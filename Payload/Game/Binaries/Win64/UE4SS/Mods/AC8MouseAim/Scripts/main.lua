-- AC8 Mouse Aim 0.1.0: offline-only attitude feed for the native input controller.
local directory = assert(debug.getinfo(1, "S").source:sub(2):match("^(.*[/\\])"))
local aim_camera = dofile(directory .. "camera.lua")
local gaze_probe = dofile(directory .. "gaze_probe.lua")
local gaze = dofile(directory .. "gaze.lua")
local start_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_start"))
local reload_native = assert(package.loadlib(directory .. "ac8_mouse_aim_010.dll", "ac8_mouseaim_reload"))
local pipe = nil
local current_address = nil
local startup_address, startup_time = nil, 0
local next_search = 0
local reported_rotation_shape = false
local camera_metric_time=0
local pause_gameplay

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
    local ok_direct, direct = pcall(function() return rotation[wanted] end)
    if ok_direct then
        local value = unwrap_number(direct)
        if value then return value end
    end

    if type(rotation) == "table" then
        local wanted_lower = string.lower(wanted)
        for key, candidate in pairs(rotation) do
            if type(key) == "string" and string.find(string.lower(key), wanted_lower, 1, true) then
                local value = unwrap_number(candidate)
                if value then return value end
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

local function close_pipe()
    if pipe then pcall(function() pipe:close() end) end
    pipe = nil
end

local function connect_pipe()
    if pipe then return true end
    local handle = io.open("\\\\.\\pipe\\AC8MouseAim", "w")
    if not handle then return false end
    handle:setvbuf("no")
    pipe = handle
    return true
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

local function camera_rotation(controller, fallback)
    local ok_manager, manager = pcall(function() return controller.PlayerCameraManager end)
    if not ok_manager or not manager or not manager:IsValid() then return fallback end
    local ok_camera, rotation = pcall(function() return manager:GetCameraRotation() end)
    if ok_camera and rotation then return rotation end
    local ok_actor, actor_rotation = pcall(function() return manager:K2_GetActorRotation() end)
    if ok_actor and actor_rotation then return actor_rotation end
    return fallback
end

RegisterKeyBind(Key.F10, function()
    local ok, err = pcall(reload_native)
    notice(ok and "Configuration reloaded; target recentered." or ("Reload failed: " .. tostring(err)))
end)
RegisterKeyBind(Key.F6, function() gaze_probe.request() end)

start_native()

if EngineTickAvailable == false or type(LoopInGameThreadAfterFrames) ~= "function" then
    notice("Disabled: required game-thread callback unavailable.")
else
    LoopInGameThreadAfterFrames(1, function()
        local ok, err = pcall(function()
            local now = os.time()
            if now < next_search then return end
            local pawn, controller = player_plane()
            if not pawn then
                gaze_probe.end_mission()
                startup_address=nil; startup_time=0
                aim_camera.restore()
                if current_address then
                    close_pipe()
                    current_address = nil
                end
                next_search = now + 1
                notice("Waiting for a single-player aircraft.")
                return
            end
            local incoming_address=pawn:GetAddress()
            if startup_address~=incoming_address then
                aim_camera.restore()
                close_pipe()
                current_address=nil
                startup_address=incoming_address
                startup_time=0
            end
            -- Allow the spawned pawn's mission transform to replace construction defaults.
            if startup_time<0.5 then
                local gameplay=StaticFindObject("/Script/Engine.Default__GameplayStatics")
                startup_time=startup_time+math.max(0,math.min(0.1,gameplay:GetWorldDeltaSeconds(pawn)))
                return
            end
            if not connect_pipe() then
                next_search = now + 1
                notice("Waiting for native controller pipe.")
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
            local gazing=gaze.update(pawn)
            local desired_camera
            if gazing then
                aim_camera.seed(controller.PlayerCameraManager:GetCameraRotation(),rotation_component)
            else
                desired_camera=aim_camera.update(pawn,controller,rotation,rotation_component,directory)
            end
            local camera = camera_rotation(controller, rotation)
            local camera_pitch = rotation_component(camera, "Pitch") or pitch
            local camera_yaw = rotation_component(camera, "Yaw") or yaw
            local camera_roll = rotation_component(camera, "Roll") or roll
            local address = pawn:GetAddress()
            assert(type(address) == "number" and address > 0, "Invalid aircraft address")
            local fov=100
            pcall(function() fov=controller.PlayerCameraManager:GetFOVAngle() end)
            local position=pawn:K2_GetActorLocation()
            local view_position=controller.PlayerCameraManager:GetCameraLocation()
            local ox=assert(rotation_component(view_position,"X"))-assert(rotation_component(position,"X"))
            local oy=assert(rotation_component(view_position,"Y"))-assert(rotation_component(position,"Y"))
            local oz=assert(rotation_component(view_position,"Z"))-assert(rotation_component(position,"Z"))
            if now-camera_metric_time>=10 then
                camera_metric_time=now
                pcall(function()
                    local v=pawn:GetVelocity()
                    local vx,vy,vz=rotation_component(v,'X'),rotation_component(v,'Y'),rotation_component(v,'Z')
                    print(string.format('[AC8MouseAim] Camera metrics: requested=%s speed=%.1fm/s fov=%.2f sampledDistance=%.2fm\n',
                        desired_camera and 'custom' or 'native',math.sqrt(vx*vx+vy*vy+vz*vz)/100,
                        fov,math.sqrt(ox*ox+oy*oy+oz*oz)/100))
                end)
            end
            if not pause_gameplay or not pause_gameplay:IsValid() then
                pause_gameplay=StaticFindObject('/Script/Engine.Default__GameplayStatics')
            end
            local paused=pause_gameplay:IsGamePaused(pawn)
            local line = (gazing and 'GAZE 1\n' or 'GAZE 0\n')..(paused and 'PAUSE 1\n' or 'PAUSE 0\n')..string.format("POSE %X %.7g %.7g %.7g %.7g %.7g %.7g %.7g %.7g %.7g %.7g\n",
                address, pitch, yaw, roll, camera_pitch, camera_yaw, camera_roll,fov,ox,oy,oz)
            if desired_camera then
                line=line..string.format("CAMERA %X %X %.9g %.9g %.9g\n",
                    controller.PlayerCameraManager:GetAddress(),address,
                    desired_camera.pitch,desired_camera.yaw,desired_camera.roll)
            else
                line=line.."CAMERA 0 0 0 0 0\n"
            end
            local wrote, write_error = pipe:write(line)
            if not wrote then
                close_pipe()
                error(write_error or "pipe write failed")
            end
            if current_address ~= address then
                current_address = address
                notice("Active for " .. pawn:GetFullName())
            end
        end)
        if not ok then
            close_pipe()
            current_address = nil
            next_search = os.time() + 1
            notice("Recovering after runtime error: " .. tostring(err))
        end
    end)
    notice("Loaded. Offline controller will activate after entering a mission.")
end
