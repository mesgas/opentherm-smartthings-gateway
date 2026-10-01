-- SmartThings Edge driver for the OpenTherm gateway (ESP32-C3).
-- Polls http://<ip>/api/state every POLL_SECONDS and sends commands with POST /api/settings.

local capabilities = require "st.capabilities"
local Driver = require "st.driver"
local log = require "log"
local cosock = require "cosock"
local http = cosock.asyncify "socket.http"
local ltn12 = require "ltn12"
local json = require "st.json"

local NS = "bookmusic32648"   -- namespace of the custom capabilities (see the README to use your own)
local POLL_SECONDS = 10

local cap = {
  modulation = capabilities[NS .. ".boilerModulation"],
  maxTemp    = capabilities[NS .. ".heatingMaxTemp"],
  hwSetpoint = capabilities[NS .. ".hotWaterSetpoint"],
  status     = capabilities[NS .. ".boilerStatus"],
}

-- ---------------------------------------------------------------------------
-- HTTP
-- ---------------------------------------------------------------------------
local function request(device, method, path, body)
  local ip = device.preferences.ipAddress
  if not ip or ip == "" then return nil, "IP address not set" end

  local headers = {}
  local source
  if body then
    local s = json.encode(body)
    headers["Content-Type"] = "application/json"
    headers["Content-Length"] = tostring(#s)
    source = ltn12.source.string(s)
  end
  local token = device.preferences.token
  if token and token ~= "" then headers["X-Token"] = token end

  local resp = {}
  local ok, code = http.request({
    url = "http://" .. ip .. path,
    method = method,
    headers = headers,
    source = source,
    sink = ltn12.sink.table(resp),
    create = function()
      local sock = cosock.socket.tcp()
      sock:settimeout(8)
      return sock
    end,
  })
  if not ok then return nil, tostring(code) end
  if code ~= 200 then return nil, "HTTP " .. tostring(code) end

  local decoded_ok, data = pcall(json.decode, table.concat(resp))
  if not decoded_ok then return nil, "invalid JSON" end
  return data
end

-- ---------------------------------------------------------------------------
-- State -> events
-- ---------------------------------------------------------------------------
local function emit(device, component_id, event)
  local component = device.profile.components[component_id]
  if component then device:emit_component_event(component, event) end
end

local function emit_temperature(device, component_id, value)
  if type(value) == "number" then
    emit(device, component_id, capabilities.temperatureMeasurement.temperature({ value = value, unit = "C" }))
  end
end

local function emit_switch(device, component_id, value)
  if type(value) == "boolean" then
    emit(device, component_id, value and capabilities.switch.switch.on() or capabilities.switch.switch.off())
  end
end

local function apply_state(device, st)
  local s = st.settings or {}
  local b = st.boiler or {}
  local t = st.thermostat or {}

  -- boiler
  emit_temperature(device, "caldaia", b.flow)
  if type(b.modulation) == "number" then
    emit(device, "caldaia", cap.modulation.modulation({ value = b.modulation, unit = "%" }))
  end
  if type(b.flame) == "boolean" then
    emit(device, "caldaia", cap.status.flame(b.flame and "on" or "off"))
  end
  if type(b.chActive) == "boolean" then
    emit(device, "caldaia", cap.status.heating(b.chActive and "active" or "idle"))
  end
  if type(b.dhwActive) == "boolean" then
    emit(device, "caldaia", cap.status.hotWater(b.dhwActive and "active" or "idle"))
  end
  if type(t.chRequest) == "boolean" then
    emit(device, "caldaia", cap.status.heatRequest(t.chRequest and "on" or "off"))
  end
  if type(b.fault) == "boolean" then
    emit(device, "caldaia", cap.status.fault(b.fault and "fault" or "ok"))
  end
  if type(b.faultCode) == "number" then
    emit(device, "caldaia", cap.status.faultCode({ value = b.faultCode }))
  end

  -- heating
  emit_switch(device, "riscaldamento", s.chEnable)
  if type(s.chMax) == "number" then
    emit(device, "riscaldamento", cap.maxTemp.maxTemp({ value = s.chMax, unit = "C" }))
  end

  -- automatic flow temperature (eco)
  emit_switch(device, "mandataauto", s.flowAuto)

  -- domestic hot water
  emit_switch(device, "sanitaria", s.dhwEnable)
  if type(s.dhwSetpoint) == "number" then
    emit(device, "sanitaria", cap.hwSetpoint.setpoint({ value = s.dhwSetpoint, unit = "C" }))
  end
  emit_temperature(device, "sanitaria", b.dhw)

  -- temperature sensors
  emit_temperature(device, "ritorno", b["return"])
  emit_temperature(device, "esterna", b.outside)

  -- house: temperature of the original thermostat + target set from SmartThings
  emit_temperature(device, "main", b.room)
  -- on/off (heating allowed or blocked)
  if type(s.chEnable) == "boolean" then
    emit(device, "main", capabilities.thermostatMode.thermostatMode(s.chEnable and "heat" or "off"))
  end
  -- "App command": on = the gateway regulates to the SmartThings target, off = the wall thermostat decides
  emit_switch(device, "comandoapp", s.roomControl)
  -- when the thermostat is in command show its own target, otherwise the one set from SmartThings
  local target = s.roomTarget
  if not s.roomControl and type(t.roomSetpoint) == "number" then target = t.roomSetpoint end
  if type(target) == "number" then
    emit(device, "main", capabilities.thermostatHeatingSetpoint.heatingSetpoint({ value = target, unit = "C" }))
  end
  local c = st.control or {}
  local heating
  if s.roomControl and c.active then
    heating = c.heating == true
  else
    heating = b.chActive == true
  end
  emit(device, "main", capabilities.thermostatOperatingState.thermostatOperatingState(heating and "heating" or "idle"))
end

-- A single timeout must not take the device offline: retry immediately,
-- and go offline only after MAX_FAILS consecutive failed reads.
local MAX_FAILS = 4

local function refresh(_, device)
  local st, err = request(device, "GET", "/api/state")
  if not st then st, err = request(device, "GET", "/api/state") end
  if st then
    device:set_field("fails", 0)
    device:online()
    apply_state(device, st)
  else
    local fails = (device:get_field("fails") or 0) + 1
    device:set_field("fails", fails)
    log.warn("gateway unreachable (" .. fails .. "): " .. tostring(err))
    if fails >= MAX_FAILS then device:offline() end
  end
end

local function send_settings(device, settings)
  local st, err = request(device, "POST", "/api/settings", settings)
  if st then
    device:online()
    apply_state(device, st)
  else
    log.error("command failed: " .. tostring(err))
  end
end

-- ---------------------------------------------------------------------------
-- Commands
-- ---------------------------------------------------------------------------
local switch_fields = { riscaldamento = "chEnable", sanitaria = "dhwEnable", mandataauto = "flowAuto", comandoapp = "roomControl" }

local function switch_handler(_, device, command)
  local field = switch_fields[command.component]
  if not field then return end
  send_settings(device, { [field] = (command.command == "on") })
end

local function max_temp_handler(_, device, command)
  send_settings(device, { chMax = command.args.value })
end

local function hw_setpoint_handler(_, device, command)
  send_settings(device, { dhwSetpoint = command.args.value })
end

-- Mode = on/off: "off" blocks heating, "heat" allows it
local function thermostat_mode_handler(_, device, command)
  local mode = command.args and command.args.mode or command.command
  if mode == "off" then
    send_settings(device, { chEnable = false })
  elseif mode == "heat" or mode == "auto" then
    send_settings(device, { chEnable = true })
  end
end

-- setting the target from the app switches to "app command"; from "off" heating stays off
local function heating_setpoint_handler(_, device, command)
  send_settings(device, { roomTarget = command.args.setpoint, roomControl = true })
end

-- ---------------------------------------------------------------------------
-- Lifecycle
-- ---------------------------------------------------------------------------
local function start_polling(driver, device)
  local timer = device:get_field("poll_timer")
  if timer then device.thread:cancel_timer(timer) end
  timer = device.thread:call_on_schedule(POLL_SECONDS, function()
    refresh(driver, device)
  end, "otgw-poll-" .. device.id)
  device:set_field("poll_timer", timer)
  device.thread:call_with_delay(1, function() refresh(driver, device) end)
end

local function device_init(driver, device)
  emit(device, "main", capabilities.thermostatMode.supportedThermostatModes({ "off", "heat" }, { visibility = { displayed = false } }))
  start_polling(driver, device)
end

local function device_info_changed(driver, device)
  start_polling(driver, device)
end

local function device_removed(_, device)
  local timer = device:get_field("poll_timer")
  if timer then device.thread:cancel_timer(timer) end
end

-- ---------------------------------------------------------------------------
-- Discovery: creates a single device; the IP is set in the device settings
-- ---------------------------------------------------------------------------
local function discovery(driver, _, _)
  if #driver:get_devices() > 0 then return end
  driver:try_create_device({
    type = "LAN",
    device_network_id = "otgw-" .. tostring(os.time()),
    label = "OpenTherm Gateway",
    profile = "opentherm-gateway",
    manufacturer = "DIY",
    model = "OpenTherm Gateway",
  })
end

local driver = Driver("opentherm-gateway", {
  discovery = discovery,
  lifecycle_handlers = {
    init = device_init,
    infoChanged = device_info_changed,
    removed = device_removed,
  },
  capability_handlers = {
    [capabilities.refresh.ID] = {
      [capabilities.refresh.commands.refresh.NAME] = refresh,
    },
    [capabilities.switch.ID] = {
      [capabilities.switch.commands.on.NAME] = switch_handler,
      [capabilities.switch.commands.off.NAME] = switch_handler,
    },
    [capabilities.thermostatMode.ID] = {
      [capabilities.thermostatMode.commands.setThermostatMode.NAME] = thermostat_mode_handler,
      [capabilities.thermostatMode.commands.off.NAME] = thermostat_mode_handler,
      [capabilities.thermostatMode.commands.auto.NAME] = thermostat_mode_handler,
      [capabilities.thermostatMode.commands.heat.NAME] = thermostat_mode_handler,
    },
    [capabilities.thermostatHeatingSetpoint.ID] = {
      [capabilities.thermostatHeatingSetpoint.commands.setHeatingSetpoint.NAME] = heating_setpoint_handler,
    },
    [cap.maxTemp.ID] = {
      [cap.maxTemp.commands.setMaxTemp.NAME] = max_temp_handler,
    },
    [cap.hwSetpoint.ID] = {
      [cap.hwSetpoint.commands.setSetpoint.NAME] = hw_setpoint_handler,
    },
  },
})

driver:run()
