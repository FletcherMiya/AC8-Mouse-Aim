-- Local fork 0.2.30-pw.11; requires its matching native DLL (bridge 41).
local directory = assert(debug.getinfo(1, "S").source:sub(2):match("^(.*[/\\])"))
local aim_camera = dofile(directory .. "camera.lua")
local gaze_probe = dofile(directory .. "gaze_probe.lua")
local gaze = dofile(directory .. "gaze.lua")
local start_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_start"))
local reload_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_reload"))
local begin_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_begin"))
local frame_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_frame"))
local camera_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_camera"))
local release_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_release"))
local perf_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_perf"))
local keys_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_keys"))
local foreground_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_foreground"))
local current_address = nil
local startup_address, startup_time = nil, 0
local next_search = 0
local reported_rotation_shape = false
local pause_gameplay
local rotation_fields={}

local function unwrap_number(value)
    if type(value) == "number" then return value end
    if type(value) == "table" or type(value) == "userdata" then
        local ok_get, getter = pcall(function() return value.get end)
        if ok_get and type(getter) == "function" then
            local ok_value, unwrapped = pcall(function() return value:get() end)
            if ok_value and type(unwrapped) == "number" then return unwrapped end
        end
    end
    return nil
end

local function rotation_component(rotation, wanted)
    -- Cache discovered spellings (AC exposes pitch/Yaw/Roll) instead of
    -- enumerating the same tables for each component every frame.
    if type(rotation)=='table' and rotation_fields[wanted] then
        local value=unwrap_number(rotation[rotation_fields[wanted]])
        if value then return value end
    end
    local ok_direct, direct = pcall(function() return rotation[wanted] end)
    if ok_direct then
        local value = unwrap_number(direct)
        if value then rotation_fields[wanted]=wanted; return value end
    end

    if type(rotation) == "table" then
        local wanted_lower = string.lower(wanted)
        for key, candidate in pairs(rotation) do
            if type(key) == "string" and string.find(string.lower(key), wanted_lower, 1, true) then
                local value = unwrap_number(candidate)
                if value then rotation_fields[wanted]=key; return value end
            end
        end
    end
    return nil
end

local function describe_rotation(rotation)
    if type(rotation) ~= "table" then return type(rotation) end
    local fields = {}
    for key, value in pairs(rotation) do
        fields[#fields + 1] = tostring(key) .. "=" .. type(value)
    end
    table.sort(fields)
    return "table{" .. table.concat(fields, ",") .. "}"
end
local last_notice = nil

local function notice(message)
    if message ~= last_notice then
        print("[AC8MouseAim] " .. message .. "\n")
        last_notice = message
    end
end

local plane_class,cached_controller
local function player_plane()
    if not plane_class or not plane_class:IsValid() then
        plane_class=StaticFindObject("/Script/Live.LivePlayerPlane")
    end
    if not plane_class or not plane_class:IsValid() then return nil end
    -- Reuse the controller throughout gameplay; read Pawn again each frame so
    -- respawn/possession changes still reach the existing lifecycle handling.
    if cached_controller and cached_controller:IsValid() then
        local pawn=cached_controller.Pawn
        if pawn and pawn:IsValid() and pawn:IsA(plane_class) then return pawn,cached_controller end
    end
    cached_controller=nil
    local selected, selected_controller, count = nil, nil, 0
    for _, controller in ipairs(FindAllOf("LivePlayerController") or {}) do
        if controller:IsValid() then
            local pawn = controller.Pawn
            if pawn and pawn:IsValid() and pawn:IsA(plane_class) then
                selected = pawn
                selected_controller = controller
                count = count + 1
            end
        end
    end
    if count == 1 then cached_controller=selected_controller; return selected, selected_controller end
    return nil, nil
end

local function camera_rotation(manager, fallback)
    local ok_camera, rotation = pcall(function() return manager:GetCameraRotation() end)
    if ok_camera and rotation then return rotation end
    local ok_actor, actor_rotation = pcall(function() return manager:K2_GetActorRotation() end)
    if ok_actor and actor_rotation then return actor_rotation end
    return fallback
end

assert(start_native(1729,0.125)==41,'AC8 local fork bridge unavailable or mismatched DLL/Lua (check native log).')
local reload_key, probe_key, perf_key = keys_native()
assert(reload_key and probe_key and perf_key, 'Native key configuration unavailable')
-- The bridge returns lua_Number (float); UE4SS requires an actual Lua integer.
local function keyboard_key(value)
    local key = math.tointeger(value)
    assert(key and key > 0 and key <= 255, 'Invalid native keyboard code')
    return key
end
reload_key, probe_key, perf_key = keyboard_key(reload_key), keyboard_key(probe_key), keyboard_key(perf_key)

RegisterKeyBind(reload_key, function()
    if foreground_native() ~= 1 then return end
    local ok, err = pcall(reload_native)
    notice(ok and "Configuration reload queued for next game frame." or ("Reload failed: " .. tostring(err)))
end)
RegisterKeyBind(probe_key, function() if foreground_native() == 1 then gaze_probe.request() end end)
RegisterKeyBind(perf_key, function() if foreground_native() == 1 then perf_native() end end)

if EngineTickAvailable == false or type(LoopInGameThreadAfterFrames) ~= "function" then
    notice("Disabled: required game-thread callback unavailable.")
else
    LoopInGameThreadAfterFrames(1, function()
        begin_native()
        local ok, err = pcall(function()
            local now = os.time()
            if now < next_search then return end
            local pawn, controller = player_plane()
            if not pawn then
                gaze_probe.end_mission()
                startup_address=nil; startup_time=0
                aim_camera.restore()
                if current_address then
                    release_native()
                    current_address = nil
                end
                next_search = now + 1
                notice("Waiting for a single-player aircraft.")
                return
            end
            local incoming_address=pawn:GetAddress()
            if startup_address~=incoming_address then
                aim_camera.restore()
                release_native()
                current_address=nil
                startup_address=incoming_address
                startup_time=0
            end
            if not pause_gameplay or not pause_gameplay:IsValid() then
                pause_gameplay=StaticFindObject('/Script/Engine.Default__GameplayStatics')
            end
            local dt=pause_gameplay:GetWorldDeltaSeconds(pawn)
            -- Allow the spawned pawn's mission transform to replace construction defaults.
            if startup_time<0.5 then
                startup_time=startup_time+math.max(0,math.min(0.1,dt))
                return
            end
            gaze_probe.update(pawn,controller,directory)
            local rotation = pawn:K2_GetActorRotation()
            assert(rotation, "K2_GetActorRotation returned nil")
            local pitch = rotation_component(rotation, "Pitch")
            local yaw = rotation_component(rotation, "Yaw")
            local roll = rotation_component(rotation, "Roll")
            if not pitch or not yaw or not roll then
                if not reported_rotation_shape then
                    reported_rotation_shape = true
                    notice("Unsupported rotation value: " .. describe_rotation(rotation))
                end
                error("Unable to read aircraft Pitch/Yaw/Roll")
            end
            local manager=controller.PlayerCameraManager
            assert(manager and manager:IsValid(),'Camera manager unavailable')
            local gazing=gaze.update(pawn,pause_gameplay:GetRealTimeSeconds(pawn))
            local camera = camera_rotation(manager, rotation)
            local camera_pitch = rotation_component(camera, "Pitch") or pitch
            local camera_yaw = rotation_component(camera, "Yaw") or yaw
            local camera_roll = rotation_component(camera, "Roll") or roll
            local address = incoming_address
            assert(type(address) == "number" and address > 0, "Invalid aircraft address")
            local fov=100
            pcall(function() fov=manager:GetFOVAngle() end)
            local position=pawn:K2_GetActorLocation()
            local view_position=manager:GetCameraLocation()
            local ox=assert(rotation_component(view_position,"X"))-assert(rotation_component(position,"X"))
            local oy=assert(rotation_component(view_position,"Y"))-assert(rotation_component(position,"Y"))
            local oz=assert(rotation_component(view_position,"Z"))-assert(rotation_component(position,"Z"))
            local paused=pause_gameplay:IsGamePaused(pawn)
            local on,target_pitch,target_yaw=frame_native(address,pitch,yaw,roll,
                camera_pitch,camera_yaw,camera_roll,fov,ox,oy,oz,paused and 1 or 0,gazing and 1 or 0)
            assert(on~=nil,'Native frame rejected')
            local desired_camera
            if gazing then
                aim_camera.seed(camera,rotation_component)
            else
                desired_camera=aim_camera.update(pawn,controller,rotation,rotation_component,
                    on,target_pitch,target_yaw,dt)
            end
            if desired_camera then
                assert(camera_native(manager:GetAddress(),address,
                    desired_camera.pitch,desired_camera.yaw,desired_camera.roll)==1,'Camera command rejected')
            else
                camera_native(0,0,0,0,0)
            end
            if current_address ~= address then
                current_address = address
                notice("Active for " .. pawn:GetFullName())
            end
        end)
        if not ok then
            release_native()
            current_address = nil
            next_search = os.time() + 1
            notice("Recovering after runtime error: " .. tostring(err))
        end
    end)
    notice("Loaded. Offline controller will activate after entering a mission.")
end
