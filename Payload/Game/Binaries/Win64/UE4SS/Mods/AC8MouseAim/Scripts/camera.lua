-- Only compute orientation; native post-update supplies current-frame position.
local M={}
local directory=assert(debug.getinfo(1,'S').source:sub(2):match('^(.*[/\\])'))
local mathrig=dofile(directory..'rig_math.lua')
local owner,forward,up,sequence,last_seen
local snapshot
local gameplay
local next_read=0
local function direction(p,y)
    p=math.rad(p); y=math.rad(y)
    return {X=math.cos(p)*math.cos(y),Y=math.cos(p)*math.sin(y),Z=math.sin(p)}
end
local function cross(a,b) return {X=a.Y*b.Z-a.Z*b.Y,Y=a.Z*b.X-a.X*b.Z,Z=a.X*b.Y-a.Y*b.X} end
local function dot(a,b) return a.X*b.X+a.Y*b.Y+a.Z*b.Z end
function M.restore() owner=nil; forward=nil; up=nil; sequence=nil; last_seen=nil; snapshot=nil; next_read=0 end
function M.seed(rotation,component)
    local p=assert(component(rotation,'Pitch'))
    local y=assert(component(rotation,'Yaw'))
    local r=math.rad(assert(component(rotation,'Roll')))
    forward=direction(p,y)
    local right={X=-math.sin(math.rad(y)),Y=math.cos(math.rad(y)),Z=0}
    local neutral_up=cross(forward,right)
    up={X=neutral_up.X*math.cos(r)+right.X*math.sin(r),
        Y=neutral_up.Y*math.cos(r)+right.Y*math.sin(r),Z=neutral_up.Z*math.cos(r)+right.Z*math.sin(r)}
end
function M.update(pawn,controller,rotation,component,path)
    local address=pawn:GetAddress()
    if owner~=address then M.restore(); owner=address end
    if not gameplay or not gameplay:IsValid() then
        gameplay=StaticFindObject('/Script/Engine.Default__GameplayStatics')
    end
    local now=gameplay:GetRealTimeSeconds(pawn)
    local line
    -- The producer publishes at ~30 Hz. Reuse its last sample between polls;
    -- orientation smoothing and the native post-camera update still run every frame.
    if now>=next_read or now<next_read-0.1 then
        next_read=now+1/30
        local f=io.open(path..'camera-target.txt','r')
        line=f and f:read('*l'); if f then f:close() end
    end
    local seq,on,p,y,ptr
    if line then seq,on,p,y,ptr=line:match('^(%d+) (%d) ([%d.eE+%-]+) ([%d.eE+%-]+) (%x+)') end
    if ptr and tonumber(p) and tonumber(y) then
        if ptr:upper()~=string.format('%X',address) then M.restore(); return nil end
        if seq~=sequence then
            sequence=seq; last_seen=now
            snapshot={on=on,p=p,y=y}
        end
    end
    -- A transient file-open/parse failure is not an explicit camera release.
    -- Keep the last complete snapshot, without renewing its expiry time.
    if not snapshot then return nil end
    on,p,y=snapshot.on,snapshot.p,snapshot.y
    if on~='1' or not last_seen or now-last_seen>2 then forward=nil; return nil end
    local target=controller:GetViewTarget()
    if not target or not target:IsValid() or target:GetAddress()~=address then forward=nil; return nil end
    if not forward then
        forward=direction(assert(component(rotation,'Pitch')),assert(component(rotation,'Yaw')))
        up={X=0,Y=0,Z=1}
    end
    local dt=gameplay:GetWorldDeltaSeconds(pawn)
    forward,up=mathrig.step(forward,up,direction(assert(tonumber(p)),assert(tonumber(y))),math.max(0,math.min(0.1,dt)))
    local pitch=math.deg(math.asin(math.max(-1,math.min(1,forward.Z))))
    local yaw=math.deg(math.atan(forward.Y,forward.X))
    local right=cross(up,forward)
    local neutral_right={X=-math.sin(math.rad(yaw)),Y=math.cos(math.rad(yaw)),Z=0}
    local neutral_up=cross(forward,neutral_right)
    local roll=math.deg(math.atan(-dot(right,neutral_up),dot(right,neutral_right)))
    return {pitch=pitch,yaw=yaw,roll=roll}
end
return M
