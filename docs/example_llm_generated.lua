-- Example Lua script generated LIVE by glm-5.2 (DashScope) via llm_loop.py
-- on 2026-08-16, from 30 deduped ambient advertisements. Deployed and
-- verified on the dongle: 68 -> 10 adv lines per 6 s.
-- Shows what the LLM leg of the product loop produces: sandbox-safe
-- (string/table/math only), 7-arg on_adv, transform(addr, json_string).

local last_emit = {}
local tick = 0
local cache = {}

function on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data)
  -- Suppress weak signals
  if rssi < -85 then return false end

  -- Suppress uninteresting devices (no name, no uuids) unless very strong
  if rssi <= -70 then
    local has_name = name and #name > 0
    local has_uuids = uuids and #uuids > 0
    if not has_name and not has_uuids then
      return false
    end
  end

  -- Throttle noisy advertisers
  tick = tick + 1
  if tick % 1000 == 0 then
    last_emit = {}
    cache = {}
  end

  local last = last_emit[addr]
  if last and tick - last < 10 then
    return false
  end
  last_emit[addr] = tick

  cache[addr] = {rssi = rssi, name = name}
  return true
end

function transform(addr, json_string)
  local data = cache[addr]
  if not data then
    return json_string
  end
  if data.name and #data.name > 0 then
    return string.format('{"addr":"%s","name":"%s","rssi":%d}', addr, data.name, math.floor(data.rssi))
  else
    return string.format('{"addr":"%s","rssi":%d}', addr, math.floor(data.rssi))
  end
end
