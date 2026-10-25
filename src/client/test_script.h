// client/test_script.h — scripted playtests (--smoke-script file.json); see test_script.cpp for the steps.
#pragma once
namespace dq {
class App;
void testScriptFrame(App& a);   // once per frame, before SDL events are polled
}
