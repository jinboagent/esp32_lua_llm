-- demo tool pack (H6.1 M1) - three advertised tools + one private helper
-- pack convention: every line is ONE complete statement, at most 240 bytes;
-- private globals carry the pack prefix (demo_) so packs never collide
DEMO_M = [[{"version":1,"name":"demo","tools":[{]]
DEMO_M = DEMO_M .. [["name":"mean","doc":"Arithmetic mean of a table of numbers.",]]
DEMO_M = DEMO_M .. [["args":[{"name":"numbers","type":"table","item_type":"number"}],]]
DEMO_M = DEMO_M .. [["returns":"string","mutating":false,]]
DEMO_M = DEMO_M .. [["example":{"args":{"numbers":[3,5,10]},"result":"6.00"}},{]]
DEMO_M = DEMO_M .. [["name":"temp_convert","doc":"Temperature conversion: unit 'c' returns Fahrenheit, 'f' returns Celsius.",]]
DEMO_M = DEMO_M .. [["args":[{"name":"value","type":"number"},{"name":"unit","type":"string","enum":["c","f"]}],]]
DEMO_M = DEMO_M .. [["returns":"string","mutating":false,]]
DEMO_M = DEMO_M .. [["example":{"args":{"value":100,"unit":"c"},"result":"212.0F"}},{]]
DEMO_M = DEMO_M .. [["name":"bench_reset","doc":"Reset the pack's demo counter (the private helper demo_bench_tick advances it).",]]
DEMO_M = DEMO_M .. [["args":[],"returns":"ack","mutating":true,]]
DEMO_M = DEMO_M .. [["example":{"args":{},"result":"ok: bench reset"}}]]}
function manifest() return DEMO_M end
demo_n = 0
function mean(a) local s=0 for i=1,#a do s=s+a[i] end return string.format("%.2f",s/#a) end
function temp_convert(a) if a.unit=="c" then return string.format("%.1fF",a.value*9/5+32) elseif a.unit=="f" then return string.format("%.1fC",(a.value-32)*5/9) end return "err: unit must be c or f" end
function bench_reset() demo_n=0 return "ok: bench reset" end
function demo_bench_tick() demo_n=demo_n+1 return demo_n end
