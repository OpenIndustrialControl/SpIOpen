# SpIOpen – Open Source Industrial IO Backplane System  
- Protocol Specification
- Electrical Specification
- Master/Slave Process Specification

**Project Goal**  
Deliver a low-cost, maker-accessible family of distributed remote IO nodes that integrate cleanly with an open source industrial control ecosystem. The design prioritizes speed of development, simplicity for contributors, electrical robustness in industrial environments, and strong alignment with existing standards (especially CANopen / CiA 301).
The project should be broken up into phases, where the first phase, "v1", is a barebones proof of concept without some electrical and software checks or nuances that would be required for a real deployment. 

**Protocol & Bus Name**  
- **Protocol**: SpIOpen  
- **Physical buses**:  
  - **MOSI Drop Bus** (master broadcast to all slaves)  
  - **MISO Chain Bus** (slave responses in daisy-chain)  
- **Why SpIOpen?**  
  - Play on CANopen → signals strong compatibility with CANopen semantics  
  - “SpI” → SPI-like physical layer  
  - “IO” embedded → clearly communicates purpose  
  - Short, memorable, brandable, logo-friendly

**Core Decisions & Trade-offs Summary**  
We chose a single-master, multiple-slave daisy-chain topology over pure multi-drop or EtherCAT to gain physical topology awareness (via TTL) and easier hot-swap/fault isolation, while avoiding the cost and complexity of dedicated fieldbus hardware. We use a custom high-speed serial protocol inspired by CANopen (FD) but transported over a clocked, point-to-multipoint MOSI Drop Bus + daisy-chain MISO Chain Bus. This gives us 10×+ the bandwidth of classic CAN while keeping per-node cost very low and avoiding proprietary silicon. The bus maintains the ability to "tunnel" CANOpen frames from CAN or CAN-FD to retain compatability with 3rd party devices (such as large, off-bus drives).

## 1. Master ↔ Slave Relationship

- **Single master, multiple slaves** (1:N topology)  
- Master is the only device that initiates communication on the MOSI drop bus and controls timing.  
- Slaves are passive responders — they only transmit on the MISO Chain Bus when forwarding received frames or when originating a response (PDO events, SDO replies, heartbeats, EMCY, etc.).  
- No peer-to-peer or multi-master arbitration — this eliminates the need for CAN-style bit dominance and collision handling.

**Why we chose this**  
- Simplifies slave firmware dramatically (no arbitration logic, no error counters per node).  
- Guarantees deterministic latency in the MOSI Drop Bus direction (broadcast).  
- Enables physical chain ordering and fault detection via TTL without special discovery modes.  
- Fits industrial reality: most automation systems are centrally controlled.

## 2. The Two Buses

### MOSI Drop Bus (Master → All Slaves – Broadcast)
- **Topology**: Multi-drop (parallel tap from master to every slave).  
- **Signals**: CLK + MOSI (single-ended 3.3 V TTL levels initially).  
- **Direction**: Master transmits only — slaves receive only.  
- **Electrical driver**: Buffered/amplified at master side (e.g. 74LVC245 or 74HC244) to drive high fan-out (up to 64+ nodes). Slaves see a strong, low-impedance source.  
- **Speed**: Target 10 MHz (conservative; hardware capable of 20–50 MHz).  
- **Termination**: **Mandatory** 100 Ω parallel termination resistor to GND on both CLK and MOSI lines at the farthest physical end (last baseplate or empty-slot cover jumper card).  
  - Why mandatory: Ensures signal integrity at 10 MHz, prevents reflections and ringing in longer chains.  
- **Hot-swap / bypass**: No bypass needed — **this** bus remains intact if a slave is removed (parallel nature).


### MISO Chain Bus (Slaves → Master – Daisy Chain)
- **Topology**: True daisy chain — each slave's upstream output connects to the next slave's upstream input. Last slave connects back to master.  
- **Signals**: CLK + MOSI (slave acts as SPI master for its output segment).  
- **Direction**: Slaves transmit only to next slave or master.  
- **Electrical**: Short segments (few cm), single-ended 3.3 V initially.  
- **Speed**: Same 10 MHz target.  
- **TTL field**: Every frame (on both buses) carries an 8-bit TTL byte (byte 1).  
  - On the chain: originating slave sets TTL (e.g. 127); each forwarding slave decrements TTL.  
  - If TTL reaches 0 → frame is dropped (loop protection).  
  - On the drop bus: master sets TTL (same header layout for DMA simplicity).  
- **Hot-swap / bypass**:  
  - v1: Manual “empty slot cover” jumper card (straight-through traces for CLK/MOSI). Will break the bus when swapped, but probably allows "live swap" during pre-op states.  
  - v2+: Automatic bypass circuit (sense pin + MOSFET or relay + watchdog) prepared but not populated in v1.  
- **Master receives**: From the last slave in chain (SPI slave mode on master).

**Why these two buses**  
- MOSI Drop Bus: Simplest way to reach all nodes simultaneously (broadcast SYNC, NMT, SDO requests).  
- MISO Chain Bus: Enables physical topology detection (TTL value = distance from master), fault localization, and cumulative responses if desired. Same frame format on both buses keeps DMA and parsing simple.
- Together: High bandwidth, low cost, and topology awareness without EtherCAT hardware overhead.

## 3. Slave Expectations – Logical & Electrical

### Logical (Protocol Behavior)
- Slaves **only react** to frames received on the **MOSI Drop Bus** (addressed to them via node ID after unpacking, or broadcast commands).  
  - They **do not forward** any of these frames downstream — downstream is receive-only.  
- On the **MISO Chain Bus**, slaves **blindly forward** all incoming upstream frames from the previous slave after decrementing TTL (drop if TTL=0).  
  - They do **not** inspect or react to the content of forwarded frames.  
  - If the slave has its own frame to send (PDO event, SDO reply, heartbeat, EMCY), it appends it to the chain (simple insert after forwarding current frame).  
- Use CANopenNode stack with custom serial transport driver.  
- Support full 64-byte PDO payloads (no splitting).  
- Node ID assigned by master via LSS (1–127 range, 7-bit), with extra logic in the master to track physical position on bus by inspecting TTL values.

### Electrical
- 3 serial interfaces:  
  1. MOSI Drop Bus RX (CLK + MOSI) – SPI slave  
  2. MISO Chain Bus RX (CLK + MOSI from previous) – SPI slave  
  3. MISO Chain Bus TX (CLK + MOSI to next) – SPI master  
- TTL decrement/drop handled in PIO (zero CPU cost).  
- CRC-32 validation in software or DMA sniffer.  
- I²C for OLED diagnostics.  
- Extra SPI/I²C for field IO peripherals (ADCs, DACs, expanders).  
- User button + 1–2 status LEDs.  
- Spare GPIOs for resets, sync, enable, fault inputs.
- USB-C for low speed serial diagnostics and programming.

## 4. Master Expectations – Logical & Electrical

### Logical
- Runs CANopenNode in **master mode** (NMT master, SDO client, LSS master).  
- Broadcasts downstream via MOSI Drop Bus (SPI master, no CS, continuous CLK/MOSI).  
- Receives from the MISO Chain Bus (SPI slave mode).  
- Parses TTL to infer chain length and node positions.  
- Uses LSS to assign node IDs in physical order (closest = lowest ID).  
- Handles full 64-byte payloads.  
- **Does not automatically forward** upstream frames back to the MOSI Drop Bus.  
  - Master parses and reacts to upstream data (collect PDOs/SDOs, handle EMCY, monitor TTL).  
  - Originates new downstream frames as needed (SYNC, targeted SDO, etc.).  
  - Future (v2+): Optional configurable relay/whitelist for pseudo-P2P (e.g., forward EMCY or specific PDOs).  
- Can run as Linux user-space daemon (CANopenLinux port + custom SPI transport).

### Electrical
- Two SPI interfaces:  
  1. MOSI Drop Bus TX – SPI master, buffered output  
  2. MISO Chain Bus RX – SPI slave  
- Strong buffering (74LVC245 or similar) on MOSI Drop Bus lines.  
- Termination (100 Ω) and pull-downs (if needed) placed on the last baseplate/slot.

## 5. Frame Format (Unified – Both Buses)

The same frame layout is used on **MOSI Drop Bus** (master → slaves) and **MISO Chain Bus** (slaves → master). A fixed 5-byte header simplifies DMA and parsing.

### Common Elements
- **Preamble**: 0xAA (1 byte)  
  - Chosen for more transitions (10101010) → better clock recovery and sync detection.  
- **CRC-32**: IEEE 802.3 polynomial, 4 bytes, appended at end.  
- **DLC**: 0–64 bytes (CAN-FD compatible), Hamming(8,4) SECDED encoded as one byte (4-bit DLC in 8-bit encoded form).  
- **CID**: 11-bit CANopen COB-ID only (no extended CAN IDs). Composed of a **4-bit command** (function code) in the **most significant bits** and a **7-bit node ID** in the **least significant bits**: `CID = (command << 7) | node_id`. The protocol library provides `spiopen_cid_from_command_node()` and defines `SPIOPEN_CID_COMMAND_SHIFT` (7), `SPIOPEN_CID_NODE_SHIFT` (0), and `SPIOPEN_CID_FLAGS_SHIFT` (11) for use with shift operators and masks.

### Frame Layout (Drop Bus and Chain Bus)
- **Byte 0**: Preamble (0xAA)
- **Byte 1**: TTL (time to live, 8 bits). Decremented before retransmit on the chain; on the drop bus the master sets it (e.g. for consistency or future use).
- **Bytes 2–3**: 11-bit CID + 5 flag bits (16 bits total, big-endian).  
  - Bits 0–10: 11-bit COB-ID (CiA 301). Within this: bits 0–6 = 7-bit node ID (LSBs), bits 7–10 = 4-bit command/function code (MSBs). So `CID = (command << 7) | node_id`.  
  - Bits 11–15: 5 protocol flags (reserved / FDF / BRS etc. for future use).  
  - Byte 2 = high 8 bits; byte 3 = low 8 bits.
- **Byte 4**: DLC (1 byte, Hamming-encoded 4-bit data length code).
- **Bytes 5–(4+N)**: Data, 0–64 bytes (length given by DLC).
- **Bytes (5+N)–(8+N)**: CRC-32 (4 bytes).

**Header size**: always 5 bytes. **Total frame**: 5 + data_len + 4 = 9–73 bytes.

**Why this format**  
- Single header size on both buses → predictable DMA transfer lengths.  
- 11-bit CID only → no extended CAN IDs; aligns with 7-bit CANopen Node ID.  
- TTL on both buses → consistent layout; chain slaves decrement TTL when forwarding.  
- Hamming-protected DLC → robust to 1-bit errors, detects 2-bit.  
- CRC-32 → strong error detection, hardware-accelerated on many MCUs.

## Rationale Summary – Why This Architecture?

- Speed to working prototypes → RP2040 + FreeRTOS + CANopenNode  
- Low cost per node → no EtherCAT HW, cheap MCUs, minimal per-baseplate parts  
- Maker accessibility → standard connectors, open schematics, manual jumper bypass in v1  
- Industrial robustness → mandatory termination, TTL for topology/fault detection, CRC-32, future differential  
- Standards alignment → full CANopen semantics (64-byte PDOs, LSS, CiA 402 ready)  
- Future-proofing → portable drivers, extensible ID, modular baseplate design  
