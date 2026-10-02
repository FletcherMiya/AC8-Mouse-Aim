local M={}
local function dot(a,b) return a.X*b.X+a.Y*b.Y+a.Z*b.Z end
local function cross(a,b) return {X=a.Y*b.Z-a.Z*b.Y,Y=a.Z*b.X-a.X*b.Z,Z=a.X*b.Y-a.Y*b.X} end
local function unit(a)
    local n=math.sqrt(dot(a,a)); assert(n>1e-9,"Degenerate camera basis")
    return {X=a.X/n,Y=a.Y/n,Z=a.Z/n}
end
local function quaternion(f,u)
    local r=unit(cross(u,f)); u=unit(cross(f,r))
    local m00,m01,m02=f.X,r.X,u.X
    local m10,m11,m12=f.Y,r.Y,u.Y
    local m20,m21,m22=f.Z,r.Z,u.Z
    local t=m00+m11+m22
    local x,y,z,w
    if t>0 then
        local s=math.sqrt(t+1)*2; w=s/4; x=(m21-m12)/s; y=(m02-m20)/s; z=(m10-m01)/s
    elseif m00>m11 and m00>m22 then
        local s=math.sqrt(1+m00-m11-m22)*2; w=(m21-m12)/s; x=s/4; y=(m01+m10)/s; z=(m02+m20)/s
    elseif m11>m22 then
        local s=math.sqrt(1+m11-m00-m22)*2; w=(m02-m20)/s; x=(m01+m10)/s; y=s/4; z=(m12+m21)/s
    else
        local s=math.sqrt(1+m22-m00-m11)*2; w=(m10-m01)/s; x=(m02+m20)/s; y=(m12+m21)/s; z=s/4
    end
    return {x,y,z,w}
end
function M.step(forward,up,target,dt)
    -- Quaternion.Slerp(current, LookRotation(target, up), 1-exp(-5*dt)).
    -- Choose the pole fallback from target direction, as MouseFlight does.
    local desired_up=math.abs(target.Z)>0.9 and up or {X=0,Y=0,Z=1}
    if dot(cross(desired_up,target),cross(desired_up,target))<1e-8 then desired_up={X=0,Y=1,Z=0} end
    local a,b=quaternion(forward,up),quaternion(target,desired_up)
    local c=0; for i=1,4 do c=c+a[i]*b[i] end
    if c<0 then c=-c; for i=1,4 do b[i]=-b[i] end end
    local t=1-math.exp(-5*dt)
    local wa,wb=1-t,t
    if c<0.9995 then
        local theta=math.acos(math.min(1,c)); local s=math.sin(theta)
        wa=math.sin((1-t)*theta)/s; wb=math.sin(t*theta)/s
    end
    local q,n={},0
    for i=1,4 do q[i]=a[i]*wa+b[i]*wb; n=n+q[i]*q[i] end
    for i=1,4 do q[i]=q[i]/math.sqrt(n) end
    local x,y,z,w=q[1],q[2],q[3],q[4]
    return {X=1-2*(y*y+z*z),Y=2*(x*y+w*z),Z=2*(x*z-w*y)},
           {X=2*(x*z+w*y),Y=2*(y*z-w*x),Z=1-2*(x*x+y*y)}
end
return M
