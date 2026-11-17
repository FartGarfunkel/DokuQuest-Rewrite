# Networking: how players reach each other, and the November 2026 investigation

This is the record of a long hunt for why friends couldn't join over the internet, what each test proved, and what the
game does now. Read it before changing anything in `src/net/`, and use the checklist at the end when someone can't join.

## How joining works now (alpha.32)

| Piece | File | What it does |
|---|---|---|
| Game traffic | `src/net/server_host.*`, `src/net/client.*` | ENet over UDP on the server's port (25580 by default, the next 9 tried if it's taken). |
| TCP side-channel | `src/net/tcp_relay.*` | The server also listens for **TCP** on the same port number, on one dual-stack socket (IPv4 and IPv6). Each TCP connection gets a loopback UDP socket that hands the game's packets to the server and back, framed as `[2-byte length][packet]`. A joining game tries UDP and TCP **at the same time** and keeps whichever answers first (UDP when both do). A friend can join with only TCP forwarded. |
| IPv6 | `src/net/tcp_relay.cpp`, `src/net/client.cpp`, `src/net/lan.cpp` | ENet itself is IPv4 only. IPv6 goes over the TCP side-channel: an address typed as `[2600:abcd::1]:25580`, or a name with only IPv6 addresses, joins over TCP. The host's screens and log show its own public IPv6 address (`internetAddress6`) when it has one. Phones on mobile data usually have one even when their IPv4 can't be reached. |
| Router opening | `src/net/portmap.*` (miniupnpc 2.3.5, fetched by `tools/fetch_deps.sh`) | When a world is opened to LAN (switchable) or a dedicated server starts, the game asks the home router over UPnP to forward the port (TCP and UDP) to **the computer that's hosting**, and to open its IPv6 firewall where the router allows it. It renews the lease every 30 minutes and takes the forward back when the server stops. If the normal broadcast search finds no router, it sends the search **straight to the default gateway** (see finding 6). The log says what the router answered. |
| Diagnostics | `server_host.cpp` | "Heard from …" for the first packet from each address (UDP), "Heard from … over TCP", why a player left (goodbye, or the connection ended without one), and the router's answers. |
| Launcher updates | `CMakeLists.txt` (`DQ_LAUNCHER_VERSION`), `src/launcher/updater.cpp` | The launcher contains the dedicated server. Its version now always equals the game's, so every update installs the whole package and the new launcher takes over at its next start (see finding 7). |

## What we found, in order (each proved by a measurement, not a guess)

1. **The game's network code was not changed between alpha.26 and alpha.27.** Join problems that started then were not caused by a code change in `src/net`.
2. **A join that "worked" from outside was really the router turning Wi-Fi traffic back inside.** The server log said `Heard from 192.168.100.1`, which is the router's own address: a phone on the same Wi-Fi joining by the public address. It proved nothing about the internet path.
3. **From outside, not one packet reached the host.** `sudo tcpdump -ni any port 25580` on the host showed nothing for a phone on mobile data or on another house's Wi-Fi. tcpdump runs below the game and before the host's firewall, so no game code can be the cause when it sees nothing.
4. **Other games hosted on a phone on mobile data were reachable; ours wasn't.** Those games speak IPv6. Mobile carriers usually put IPv4 behind shared addressing (CGNAT) with no way in, but give each phone a public IPv6 address. ENet is IPv4 only, so we added IPv6 over the TCP side-channel.
5. **The internet provider does not block incoming connections, and the router can forward.** The router answered UPnP with a real public WAN address (45.59.97.225, not CGNAT). A temporary UPnP forward of TCP 25590 to the dev PC was reached by an outside port-check service at once.
6. **The router never answered the hosting PC's broadcast search.** On the PC that hosts (Wi-Fi), `upnpc -l` found only another computer's UPnP, not the router. The router answered the same search **sent straight to it** (`upnpc -u http://192.168.100.1:<port>/rootDesc.xml`). A forward made that way was reached from outside immediately. Some Wi-Fi setups never deliver broadcast/multicast to some clients. The likeliest reason the router's **manual** forward to that PC never delivered anything is the same Wi-Fi path, though that's not proven. The game now falls back to asking the gateway directly.
7. **The launcher had never updated itself since alpha.21.** `DQ_LAUNCHER_VERSION` was set apart from the game's version and never raised. The updater only fetches the whole package (with the launcher) when the release's launcher is newer, so it always fetched just the game. The launcher's built-in dedicated server stayed at whatever AppImage was started. A server log line in old wording (`Listening on TCP port … too (for networks that block UDP)`) gave it away.
8. **A stray test server held port 25580 on the dev PC for two days.** A `dokuquest-server` started from the build folder during testing made every later server on that PC move to 25581 ("Port 25580 was taken"). Stop leftover servers after testing (`ss -ulpn | grep 2558` shows who holds the ports).
9. **The router's manual forward pointed at a different PC than the one some tests ran on.** Check `ip -4 -br addr` on the hosting PC against the forward's target.

## When someone can't join: the checklist

1. **Versions.** Host and friend on the same version. The host's launcher too: the server log's first lines show its wording; the launcher's title shows its version.
2. **The host's log.**
   - "Port … was taken": another program (often a leftover server) holds the port.
   - "The router forwarded … to this computer": the router opened the way.
   - "No router that takes automatic port requests was found": forward by hand, or ask the router directly with `upnpc -u http://<gateway>:<port>/rootDesc.xml -r <port> TCP <port> UDP` (find the URL with `upnpc -l` from another computer).
   - "The router's own internet address is … a private one": a second router or CGNAT.
3. **Does anything arrive?** On the host, `sudo tcpdump -ni any port <port>` while the friend tries.
   - **Nothing:** the packets never reach the host. Check the router forward's target address, Wi-Fi vs cable, and the provider.
   - **Something, but the game log is silent:** the host's firewall. On openSUSE run `sudo firewall-cmd --permanent --add-port=<port>/tcp --add-port=<port>/udp && sudo firewall-cmd --reload`.
4. **From outside, without the friend:** an online port checker can test TCP to `<public ip>:<port>` while the server runs. That only tests TCP, but the game can run entirely on TCP now.
5. **The friend's side.** Their log's `net:` lines say which address they dialled, and over what: "connected … over TCP", "join failed: …".
6. **Hosting from a phone, or anywhere with IPv6:** give the friend the `[IPv6]:port` address the host screen shows.

## Ideas not done yet

- **Dual-stack UDP:** a maintained IPv6-capable ENet fork (MIT) would let normal UDP play use IPv6 too.
- **Invite codes:** one code listing every address that might work. The friend's game tries them all at once.
- **UDP hole punching:** free public STUN servers can tell a player their own public address. Two players behind ordinary NAT could then reach each other, but signalling still needs a code swap or a server.
- **A relay:** the only thing that works when both sides are behind CGNAT with no IPv6, and it needs a server someone runs.
