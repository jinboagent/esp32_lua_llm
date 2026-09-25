-- hwio tool pack (H6.1 M3): wraps the hw.* device bindings
-- (needs CONFIG_LUA_HW_BINDINGS; degrades to error strings when off)
-- convention: one complete statement per line, <= 240 bytes, hwio_ prefix
-- on private globals; degrade-first so the pack works on any build
HWIO_M = [[{"version":1,"name":"hwio","tools":[{]]
HWIO_M = HWIO_M .. [["name":"uptime_ms","doc":"Milliseconds since device boot.",]]
HWIO_M = HWIO_M .. [["args":[],"returns":"value","mutating":false,]]
HWIO_M = HWIO_M .. [["example":{"args":{},"result":"12345 ms"}},{]]
HWIO_M = HWIO_M .. [["name":"pin_read","doc":"Read a whitelisted GPIO pin (1-18, 21-25, 38-48).",]]
HWIO_M = HWIO_M .. [["args":[{"name":"pin","type":"number"}],"returns":"value","mutating":false,]]
HWIO_M = HWIO_M .. [["example":{"args":{"pin":4},"result":"pin 4 = 1"}},{]]
HWIO_M = HWIO_M .. [["name":"pin_write","doc":"Drive a whitelisted GPIO pin to 0 or 1.",]]
HWIO_M = HWIO_M .. [["args":[{"name":"pin","type":"number"},{"name":"value","type":"number","enum":[0,1]}],]]
HWIO_M = HWIO_M .. [["returns":"ack","mutating":true,]]
HWIO_M = HWIO_M .. [["example":{"args":{"pin":4,"value":1},"result":"ok: pin 4 = 1"}},{]]
HWIO_M = HWIO_M .. [["name":"adc_raw","doc":"Raw 12-bit ADC1 reading (gpio 1..10 on ESP32-S3).",]]
HWIO_M = HWIO_M .. [["args":[{"name":"pin","type":"number"}],"returns":"value","mutating":false,]]
HWIO_M = HWIO_M .. [["example":{"args":{"pin":5},"result":"adc1 gpio5 raw 1873"}},{]]
HWIO_M = HWIO_M .. [["name":"cfg_set","doc":"Persist a device parameter (survives reboot - the configure mode).",]]
HWIO_M = HWIO_M .. [["args":[{"name":"key","type":"string"},{"name":"value","type":"string"}],]]
HWIO_M = HWIO_M .. [["returns":"ack","mutating":true,]]
HWIO_M = HWIO_M .. [["example":{"args":{"key":"name","value":"sniff1"},"result":"ok: name=sniff1"}},{]]
HWIO_M = HWIO_M .. [["name":"cfg_get","doc":"Read a persisted device parameter.",]]
HWIO_M = HWIO_M .. [["args":[{"name":"key","type":"string"}],"returns":"value","mutating":false,]]
HWIO_M = HWIO_M .. [["example":{"args":{"key":"name"},"result":"name=sniff1"}}]}]]
function manifest() return HWIO_M end
function uptime_ms(a) if hw==nil then return "err: hw bindings off" end return tostring(hw.millis()) .. " ms" end
function pin_read(a) if hw==nil then return "err: hw bindings off" end return "pin " .. a.pin .. " = " .. tostring(hw.gpio_read(a.pin)) end
function pin_write(a) if hw==nil then return "err: hw bindings off" end hw.gpio_write(a.pin, a.value) return "ok: pin " .. a.pin .. " = " .. a.value end
function adc_raw(a) if hw==nil then return "err: hw bindings off" end return "adc1 gpio" .. a.pin .. " raw " .. tostring(hw.adc_read(a.pin)) end
function cfg_set(a) if hw==nil then return "err: hw bindings off" end hw.kv_set(a.key, a.value) return "ok: " .. a.key .. "=" .. a.value end
function cfg_get(a) if hw==nil then return "err: hw bindings off" end local v=hw.kv_get(a.key) if v==nil then return a.key .. ": not set" end return a.key .. "=" .. v end
