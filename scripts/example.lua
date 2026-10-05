assert(type(younkoo) == "table", "younkoo global missing")
assert(type(younkoo.log) == "function", "log missing")
assert(type(younkoo.get_camera) == "function", "get_camera missing")
assert(type(younkoo.register_module) == "function", "register_module missing")

local MODULE_ID = "example_module"
local ticks = 0

function on_enable()
    younkoo.log("example: on_enable")

    local ok, err = younkoo.register_module({
        id = MODULE_ID,
        name = "Example Module",
        values = { ["Interval"] = 100 },
    })
    if not ok then
        younkoo.log("example: register_module failed: " .. tostring(err))
    end
end

function on_update()
    ticks = ticks + 1
    if ticks % 100 ~= 0 then return end

    local cam = younkoo.get_camera()
    if cam and cam.valid then
        younkoo.log(string.format("example: camera %.1f %.1f %.1f yaw=%.1f pitch=%.1f",
            cam.x or 0, cam.y or 0, cam.z or 0, cam.yaw or 0, cam.pitch or 0))
    end
end

function on_disable()
    younkoo.log("example: on_disable")
end
