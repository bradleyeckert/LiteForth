# LiteForth

Cheap MCUs are now as powerful the full-fledged computers of the 1990s.
The value proposition of Forth cross compilers came and went.
Applications are now better-developed within the target MCU.
A typical setup includes:

```mermaid
flowchart LR
    %% Components
    Term[💻 Terminal Emulator]
    MCU[🎛️ Microcontroller]
    SD[💾 microSD Card]

    %% Connections
    Term <== Serial or USB Cable ==> MCU
    MCU <== SPI/SDIO ==> SD

    %% Subgraphs for visual grouping
    subgraph Host System
        Term
    end

    subgraph Embedded System
        MCU
        SD
    end

    %% Component Styling
    style Term fill:#e1f5fe,stroke:#03a9f4,stroke-width:2px,color:#000
    style MCU fill:#efebe9,stroke:#795548,stroke-width:2px,color:#000
    style SD fill:#fff3e0,stroke:#ff9800,stroke-width:2px,color:#000
    
    %% Group Styling
    %% style Host System fill:none,stroke:#ccc,stroke-dasharray: 5 5
    %% style Embedded System fill:none,stroke:#ccc,stroke-dasharray: 5 5
```

For development without MCU hardware, a LiteForth instance can
run in a terminal window, with a UART-to-terminal bridge utility
(or just a terminal emulator) running in another window.
The two windows are interconnected by a virtual COM port such as com0com.

```mermaid
flowchart LR
    %% Components
    subgraph Host System [PC Host Environment]
        %% Software Windows
        subgraph Win1 [Window 1]
            Term[💻 Terminal Emulator]
        end

        subgraph Win2 [Window 2]
            LF[📟 LiteForth Console App]
        end

        %% Virtual Connection Bridge
        subgraph VCom [Virtual COM Port Bridge e.g., com0com]
            PortA[COM A] <--> PortB[COM B]
        end
    end

    %% File System Integration
    SD[💾 Simulated microSD Card / Image File]

    %% Data Flow Connections
    Term <--> PortA
    PortB <--> LF
    LF <--> SD

    %% Styling
    style Term fill:#e1f5fe,stroke:#03a9f4,stroke-width:2px,color:#000
    style LF fill:#efebe9,stroke:#795548,stroke-width:2px,color:#000
    style SD fill:#fff3e0,stroke:#ff9800,stroke-width:2px,color:#000
    style PortA fill:#e8f5e9,stroke:#4caf50,color:#000
    style PortB fill:#e8f5e9,stroke:#4caf50,color:#000
    
    %% Bounding Box Styling
    %% style Host System fill:none,stroke:#bbb,stroke-dasharray: 5 5
    style Win1 fill:none,stroke:#b2dfdb
    style Win2 fill:none,stroke:#b2dfdb
    style VCom fill:#fafafa,stroke:#9e9e9e,stroke-dasharray: 3 3
```
The architecture of LiteForth is based on C, allowing it to be extended with
C-based functions where speed is needed. Token threading is used to minimize
code size, so more functionality can be packed into the small MCU environment.
The token interpreter also acts as a sandbox, providing better security than native code.

A key feature of LiteForth is a Root of Trust (RoT) architecture.
Once a trusted application is in place, it is made permanent by digitally signing it.
The immutable RoT section checks the signature after every hard reset ensure
the code has not changed.
While Flash in a locked down MCU is very hard to tamper,
signing covers yet-unknown side-channel attacks.
