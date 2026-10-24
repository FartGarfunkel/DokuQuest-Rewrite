set -e
cd "$(dirname "$0")/../third_party"
c() { [ -d "$2" ] || { git clone -q --depth 1 --branch "$3" "$1" "$2" && rm -rf "$2/.git"; }; echo "ok $2"; }
c https://github.com/libsdl-org/SDL.git SDL2 release-2.30.9
c https://github.com/ocornut/imgui.git imgui v1.91.5
c https://github.com/mackron/miniaudio.git miniaudio 0.11.21
c https://github.com/lsalzman/enet.git enet v1.3.18
c https://github.com/quickjs-ng/quickjs.git quickjs v0.8.0
c https://github.com/nothings/stb.git stb master
c https://github.com/g-truc/glm.git glm 1.0.1
c https://github.com/richgel999/miniz.git miniz 3.0.2
c https://github.com/nlohmann/json.git json v3.11.3
# nlohmann/json: keep only the single header as json/nlohmann/json.hpp
if [ -d json/single_include ]; then mv json/single_include/nlohmann json_nl && rm -rf json && mkdir -p json && mv json_nl json/nlohmann; fi
rm -rf SDL2/test glm/test imgui/examples quickjs/tests 2>/dev/null || true
