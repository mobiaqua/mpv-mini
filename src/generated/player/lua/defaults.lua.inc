// Generated from player/lua/defaults.lua

"-- Compatibility shim for lua 5.2/5.3\n"
"unpack = unpack or table.unpack\n"
"\n"
"-- these are used internally by lua.c\n"
"mp.UNKNOWN_TYPE.info = \"this value is inserted if the C type is not supported\"\n"
"mp.UNKNOWN_TYPE.type = \"UNKNOWN_TYPE\"\n"
"\n"
"mp.ARRAY.info = \"native array\"\n"
"mp.ARRAY.type = \"ARRAY\"\n"
"\n"
"mp.MAP.info = \"native map\"\n"
"mp.MAP.type = \"MAP\"\n"
"\n"
"function mp.get_script_name()\n"
"    return mp.script_name\n"
"end\n"
"\n"
"function mp.get_opt(key, def)\n"
"    local opts = mp.get_property_native(\"options/script-opts\")\n"
"    local val = opts[key]\n"
"    if val == nil then\n"
"        val = def\n"
"    end\n"
"    return val\n"
"end\n"
"\n"
"function mp.input_define_section(section, contents, flags)\n"
"    if flags == nil or flags == \"\" then\n"
"        flags = \"default\"\n"
"    end\n"
"    mp.commandv(\"define-section\", section, contents, flags)\n"
"end\n"
"\n"
"function mp.input_enable_section(section, flags)\n"
"    if flags == nil then\n"
"        flags = \"\"\n"
"    end\n"
"    mp.commandv(\"enable-section\", section, flags)\n"
"end\n"
"\n"
"function mp.input_disable_section(section)\n"
"    mp.commandv(\"disable-section\", section)\n"
"end\n"
"\n"
"function mp.get_mouse_pos()\n"
"    local m = mp.get_property_native(\"mouse-pos\")\n"
"    return m.x, m.y\n"
"end\n"
"\n"
"-- For dispatching script-binding. This is sent as:\n"
"--      script-message-to \044script_name \044binding_name \044keystate\n"
"-- The array is indexed by \044binding_name, and has functions like this as value:\n"
"--      fn(\044binding_name, \044keystate)\n"
"local dispatch_key_bindings = {}\n"
"\n"
"local message_id = 0\n"
"local function reserve_binding()\n"
"    message_id = message_id + 1\n"
"    return \"__keybinding\" .. tostring(message_id)\n"
"end\n"
"\n"
"local function dispatch_key_binding(name, state, key_name, key_text)\n"
"    local fn = dispatch_key_bindings[name]\n"
"    if fn then\n"
"        fn(name, state, key_name, key_text)\n"
"    end\n"
"end\n"
"\n"
"-- \"Old\", deprecated API\n"
"\n"
"-- each script has its own section, so that they don't conflict\n"
"local default_section = \"input_dispatch_\" .. mp.script_name\n"
"\n"
"-- Set the list of key bindings. These will override the user's bindings, so\n"
"-- you should use this sparingly.\n"
"-- A call to this function will remove all bindings previously set with this\n"
"-- function. For example, set_key_bindings({}) would remove all script defined\n"
"-- key bindings.\n"
"-- Note: the bindings are not active by default. Use enable_key_bindings().\n"
"--\n"
"-- list is an array of key bindings, where each entry is an array as follow:\n"
"--      {key, callback_press, callback_down, callback_up}\n"
"-- key is the key string as used in input.conf, like \"ctrl+a\"\n"
"--\n"
"-- callback can be a string too, in which case the following will be added like\n"
"-- an input.conf line: key .. \" \" .. callback\n"
"-- (And callback_down is ignored.)\n"
"function mp.set_key_bindings(list, section, flags)\n"
"    local cfg = \"\"\n"
"    for i = 1, #list do\n"
"        local entry = list[i]\n"
"        local key = entry[1]\n"
"        local cb = entry[2]\n"
"        local cb_down = entry[3]\n"
"        local cb_up = entry[4]\n"
"        if type(cb) ~= \"string\" then\n"
"            local mangle = reserve_binding()\n"
"            dispatch_key_bindings[mangle] = function(name, state)\n"
"                local event = state:sub(1, 1)\n"
"                local is_mouse = state:sub(2, 2) == \"m\"\n"
"                local def = (is_mouse and \"u\") or \"d\"\n"
"                if event == \"r\" then\n"
"                    return\n"
"                end\n"
"                if event == \"p\" and cb then\n"
"                    cb()\n"
"                elseif event == \"d\" and cb_down then\n"
"                    cb_down()\n"
"                elseif event == \"u\" and cb_up then\n"
"                    cb_up()\n"
"                elseif event == def and cb then\n"
"                    cb()\n"
"                end\n"
"            end\n"
"            cfg = cfg .. key .. \" script-binding \" ..\n"
"                  mp.script_name .. \"/\" .. mangle .. \"\\n\"\n"
"        else\n"
"            cfg = cfg .. key .. \" \" .. cb .. \"\\n\"\n"
"        end\n"
"    end\n"
"    mp.input_define_section(section or default_section, cfg, flags)\n"
"end\n"
"\n"
"function mp.enable_key_bindings(section, flags)\n"
"    mp.input_enable_section(section or default_section, flags)\n"
"end\n"
"\n"
"function mp.disable_key_bindings(section)\n"
"    mp.input_disable_section(section or default_section)\n"
"end\n"
"\n"
"function mp.set_mouse_area(x0, y0, x1, y1, section)\n"
"    mp.input_set_section_mouse_area(section or default_section, x0, y0, x1, y1)\n"
"end\n"
"\n"
"-- \"Newer\" and more convenient API\n"
"\n"
"local key_bindings = {}\n"
"local key_binding_counter = 0\n"
"local key_bindings_dirty = false\n"
"\n"
"function mp.flush_keybindings()\n"
"    if not key_bindings_dirty then\n"
"        return\n"
"    end\n"
"    key_bindings_dirty = false\n"
"\n"
"    for i = 1, 2 do\n"
"        local section, flags\n"
"        local def = i == 1\n"
"        if def then\n"
"            section = \"input_\" .. mp.script_name\n"
"            flags = \"default\"\n"
"        else\n"
"            section = \"input_forced_\" .. mp.script_name\n"
"            flags = \"force\"\n"
"        end\n"
"        local bindings = {}\n"
"        for k, v in pairs(key_bindings) do\n"
"            if v.bind and v.forced ~= def then\n"
"                bindings[#bindings + 1] = v\n"
"            end\n"
"        end\n"
"        table.sort(bindings, function(a, b)\n"
"            return a.priority < b.priority\n"
"        end)\n"
"        local cfg = \"\"\n"
"        for _, v in ipairs(bindings) do\n"
"            cfg = cfg .. v.bind .. \"\\n\"\n"
"        end\n"
"        mp.input_define_section(section, cfg, flags)\n"
"        -- TODO: remove the section if the script is stopped\n"
"        mp.input_enable_section(section, \"allow-hide-cursor+allow-vo-dragging\")\n"
"    end\n"
"end\n"
"\n"
"local function add_binding(attrs, key, name, fn, rp)\n"
"    if (type(name) ~= \"string\") and (name ~= nil) then\n"
"        rp = fn\n"
"        fn = name\n"
"        name = nil\n"
"    end\n"
"    rp = rp or \"\"\n"
"    if name == nil then\n"
"        name = reserve_binding()\n"
"    end\n"
"    local repeatable = rp == \"repeatable\" or rp[\"repeatable\"]\n"
"    if rp[\"forced\"] then\n"
"        attrs.forced = true\n"
"    end\n"
"    local key_cb, msg_cb\n"
"    if not fn then\n"
"        fn = function() end\n"
"    end\n"
"    if rp[\"complex\"] then\n"
"        local key_states = {\n"
"            [\"u\"] = \"up\",\n"
"            [\"d\"] = \"down\",\n"
"            [\"r\"] = \"repeat\",\n"
"            [\"p\"] = \"press\",\n"
"        }\n"
"        key_cb = function(name, state, key_name, key_text)\n"
"            if key_text == \"\" then\n"
"                key_text = nil\n"
"            end\n"
"            fn({\n"
"                event = key_states[state:sub(1, 1)] or \"unknown\",\n"
"                is_mouse = state:sub(2, 2) == \"m\",\n"
"                key_name = key_name,\n"
"                key_text = key_text,\n"
"            })\n"
"        end\n"
"        msg_cb = function()\n"
"            fn({event = \"press\", is_mouse = false})\n"
"        end\n"
"    else\n"
"        key_cb = function(name, state)\n"
"            -- Emulate the same semantics as input.c uses for most bindings:\n"
"            -- For keyboard, \"down\" runs the command, \"up\" does nothing;\n"
"            -- for mouse, \"down\" does nothing, \"up\" runs the command.\n"
"            -- Also, key repeat triggers the binding again.\n"
"            local event = state:sub(1, 1)\n"
"            local is_mouse = state:sub(2, 2) == \"m\"\n"
"            if event == \"r\" and not repeatable then\n"
"                return\n"
"            end\n"
"            if is_mouse and (event == \"u\" or event == \"p\") then\n"
"                fn()\n"
"            elseif (not is_mouse) and (event == \"d\" or event == \"r\" or event == \"p\") then\n"
"                fn()\n"
"            end\n"
"        end\n"
"        msg_cb = fn\n"
"    end\n"
"    if key and #key > 0 then\n"
"        attrs.bind = key .. \" script-binding \" .. mp.script_name .. \"/\" .. name\n"
"    end\n"
"    attrs.name = name\n"
"    -- new bindings override old ones (but do not overwrite them)\n"
"    key_binding_counter = key_binding_counter + 1\n"
"    attrs.priority = key_binding_counter\n"
"    key_bindings[name] = attrs\n"
"    key_bindings_dirty = true\n"
"    dispatch_key_bindings[name] = key_cb\n"
"    mp.register_script_message(name, msg_cb)\n"
"end\n"
"\n"
"function mp.add_key_binding(...)\n"
"    add_binding({forced=false}, ...)\n"
"end\n"
"\n"
"function mp.add_forced_key_binding(...)\n"
"    add_binding({forced=true}, ...)\n"
"end\n"
"\n"
"function mp.remove_key_binding(name)\n"
"    key_bindings[name] = nil\n"
"    dispatch_key_bindings[name] = nil\n"
"    key_bindings_dirty = true\n"
"    mp.unregister_script_message(name)\n"
"end\n"
"\n"
"local timers = {}\n"
"\n"
"local timer_mt = {}\n"
"timer_mt.__index = timer_mt\n"
"\n"
"function mp.add_timeout(seconds, cb)\n"
"    local t = mp.add_periodic_timer(seconds, cb)\n"
"    t.oneshot = true\n"
"    return t\n"
"end\n"
"\n"
"function mp.add_periodic_timer(seconds, cb)\n"
"    local t = {\n"
"        timeout = seconds,\n"
"        cb = cb,\n"
"        oneshot = false,\n"
"    }\n"
"    setmetatable(t, timer_mt)\n"
"    t:resume()\n"
"    return t\n"
"end\n"
"\n"
"function timer_mt.stop(t)\n"
"    if timers[t] then\n"
"        timers[t] = nil\n"
"        t.next_deadline = t.next_deadline - mp.get_time()\n"
"    end\n"
"end\n"
"\n"
"function timer_mt.kill(t)\n"
"    timers[t] = nil\n"
"    t.next_deadline = nil\n"
"end\n"
"mp.cancel_timer = timer_mt.kill\n"
"\n"
"function timer_mt.resume(t)\n"
"    if not timers[t] then\n"
"        local timeout = t.next_deadline\n"
"        if timeout == nil then\n"
"            timeout = t.timeout\n"
"        end\n"
"        t.next_deadline = mp.get_time() + timeout\n"
"        timers[t] = t\n"
"    end\n"
"end\n"
"\n"
"function timer_mt.is_enabled(t)\n"
"    return timers[t] ~= nil\n"
"end\n"
"\n"
"-- Return the timer that expires next.\n"
"local function get_next_timer()\n"
"    local best = nil\n"
"    for t, _ in pairs(timers) do\n"
"        if (best == nil) or (t.next_deadline < best.next_deadline) then\n"
"            best = t\n"
"        end\n"
"    end\n"
"    return best\n"
"end\n"
"\n"
"function mp.get_next_timeout()\n"
"    local timer = get_next_timer()\n"
"    if not timer then\n"
"        return\n"
"    end\n"
"    local now = mp.get_time()\n"
"    return timer.next_deadline - now\n"
"end\n"
"\n"
"-- Run timers that have met their deadline at the time of invocation.\n"
"-- Return: time>0 in seconds till the next due timer, 0 if there are due timers\n"
"--         (aborted to avoid infinite loop), or nil if no timers\n"
"local function process_timers()\n"
"    local t0 = nil\n"
"    while true do\n"
"        local timer = get_next_timer()\n"
"        if not timer then\n"
"            return\n"
"        end\n"
"        local now = mp.get_time()\n"
"        local wait = timer.next_deadline - now\n"
"        if wait > 0 then\n"
"            return wait\n"
"        else\n"
"            if not t0 then\n"
"                t0 = now  -- first due callback: always executes, remember t0\n"
"            elseif timer.next_deadline > t0 then\n"
"                -- don't block forever with slow callbacks and endless timers.\n"
"                -- we'll continue right after checking mpv events.\n"
"                return 0\n"
"            end\n"
"\n"
"            if timer.oneshot then\n"
"                timer:kill()\n"
"            else\n"
"                timer.next_deadline = now + timer.timeout\n"
"            end\n"
"            timer.cb()\n"
"        end\n"
"    end\n"
"end\n"
"\n"
"local messages = {}\n"
"\n"
"function mp.register_script_message(name, fn)\n"
"    messages[name] = fn\n"
"end\n"
"\n"
"function mp.unregister_script_message(name)\n"
"    messages[name] = nil\n"
"end\n"
"\n"
"local function message_dispatch(ev)\n"
"    if #ev.args > 0 then\n"
"        local handler = messages[ev.args[1]]\n"
"        if handler then\n"
"            handler(unpack(ev.args, 2))\n"
"        end\n"
"    end\n"
"end\n"
"\n"
"local property_id = 0\n"
"local properties = {}\n"
"\n"
"function mp.observe_property(name, t, cb)\n"
"    local id = property_id + 1\n"
"    property_id = id\n"
"    properties[id] = cb\n"
"    mp.raw_observe_property(id, name, t)\n"
"end\n"
"\n"
"function mp.unobserve_property(cb)\n"
"    for prop_id, prop_cb in pairs(properties) do\n"
"        if cb == prop_cb then\n"
"            properties[prop_id] = nil\n"
"            mp.raw_unobserve_property(prop_id)\n"
"        end\n"
"    end\n"
"end\n"
"\n"
"local function property_change(ev)\n"
"    local prop = properties[ev.id]\n"
"    if prop then\n"
"        prop(ev.name, ev.data)\n"
"    end\n"
"end\n"
"\n"
"-- used by default event loop (mp_event_loop()) to decide when to quit\n"
"mp.keep_running = true\n"
"\n"
"local event_handlers = {}\n"
"\n"
"function mp.register_event(name, cb)\n"
"    local list = event_handlers[name]\n"
"    if not list then\n"
"        list = {}\n"
"        event_handlers[name] = list\n"
"    end\n"
"    list[#list + 1] = cb\n"
"    return mp.request_event(name, true)\n"
"end\n"
"\n"
"function mp.unregister_event(cb)\n"
"    for name, sub in pairs(event_handlers) do\n"
"        local found = false\n"
"        for i, e in ipairs(sub) do\n"
"            if e == cb then\n"
"                found = true\n"
"                break\n"
"            end\n"
"        end\n"
"        if found then\n"
"            -- create a new array, just in case this function was called\n"
"            -- from an event handler\n"
"            local new = {}\n"
"            for i = 1, #sub do\n"
"                if sub[i] ~= cb then\n"
"                    new[#new + 1] = sub[i]\n"
"                end\n"
"            end\n"
"            event_handlers[name] = new\n"
"            if #new == 0 then\n"
"                mp.request_event(name, false)\n"
"            end\n"
"        end\n"
"    end\n"
"end\n"
"\n"
"-- default handlers\n"
"mp.register_event(\"shutdown\", function() mp.keep_running = false end)\n"
"mp.register_event(\"client-message\", message_dispatch)\n"
"mp.register_event(\"property-change\", property_change)\n"
"\n"
"-- called before the event loop goes back to sleep\n"
"local idle_handlers = {}\n"
"\n"
"function mp.register_idle(cb)\n"
"    idle_handlers[#idle_handlers + 1] = cb\n"
"end\n"
"\n"
"function mp.unregister_idle(cb)\n"
"    local new = {}\n"
"    for _, handler in ipairs(idle_handlers) do\n"
"        if handler ~= cb then\n"
"            new[#new + 1] = handler\n"
"        end\n"
"    end\n"
"    idle_handlers = new\n"
"end\n"
"\n"
"-- sent by \"script-binding\"\n"
"mp.register_script_message(\"key-binding\", dispatch_key_binding)\n"
"\n"
"mp.msg = {\n"
"    log = mp.log,\n"
"    fatal = function(...) return mp.log(\"fatal\", ...) end,\n"
"    error = function(...) return mp.log(\"error\", ...) end,\n"
"    warn = function(...) return mp.log(\"warn\", ...) end,\n"
"    info = function(...) return mp.log(\"info\", ...) end,\n"
"    verbose = function(...) return mp.log(\"v\", ...) end,\n"
"    debug = function(...) return mp.log(\"debug\", ...) end,\n"
"    trace = function(...) return mp.log(\"trace\", ...) end,\n"
"}\n"
"\n"
"_G.print = mp.msg.info\n"
"\n"
"package.loaded[\"mp\"] = mp\n"
"package.loaded[\"mp.msg\"] = mp.msg\n"
"\n"
"function mp.wait_event(t)\n"
"    local r = mp.raw_wait_event(t)\n"
"    if r and r.file_error and not r.error then\n"
"        -- compat; deprecated\n"
"        r.error = r.file_error\n"
"    end\n"
"    return r\n"
"end\n"
"\n"
"_G.mp_event_loop = function()\n"
"    mp.dispatch_events(true)\n"
"end\n"
"\n"
"local function call_event_handlers(e)\n"
"    local handlers = event_handlers[e.event]\n"
"    if handlers then\n"
"        for _, handler in ipairs(handlers) do\n"
"            handler(e)\n"
"        end\n"
"    end\n"
"end\n"
"\n"
"mp.use_suspend = false\n"
"\n"
"local suspend_warned = false\n"
"\n"
"function mp.dispatch_events(allow_wait)\n"
"    local more_events = true\n"
"    if mp.use_suspend then\n"
"        if not suspend_warned then\n"
"            mp.msg.error(\"mp.use_suspend is now ignored.\")\n"
"            suspend_warned = true\n"
"        end\n"
"    end\n"
"    while mp.keep_running do\n"
"        local wait = 0\n"
"        if not more_events then\n"
"            wait = process_timers() or 1e20 -- infinity for all practical purposes\n"
"            if wait ~= 0 then\n"
"                local idle_called = nil\n"
"                for _, handler in ipairs(idle_handlers) do\n"
"                    idle_called = true\n"
"                    handler()\n"
"                end\n"
"                if idle_called then\n"
"                    -- handlers don't complete in 0 time, and may modify timers\n"
"                    wait = mp.get_next_timeout() or 1e20\n"
"                    if wait < 0 then\n"
"                        wait = 0\n"
"                    end\n"
"                end\n"
"            end\n"
"            if allow_wait ~= true then\n"
"                return\n"
"            end\n"
"        end\n"
"        local e = mp.wait_event(wait)\n"
"        more_events = false\n"
"        if e.event ~= \"none\" then\n"
"            call_event_handlers(e)\n"
"            more_events = true\n"
"        end\n"
"    end\n"
"end\n"
"\n"
"mp.register_idle(mp.flush_keybindings)\n"
"\n"
"-- additional helpers\n"
"\n"
"function mp.osd_message(text, duration)\n"
"    if not duration then\n"
"        duration = \"-1\"\n"
"    else\n"
"        duration = tostring(math.floor(duration * 1000))\n"
"    end\n"
"    mp.commandv(\"show-text\", text, duration)\n"
"end\n"
"\n"
"local hook_table = {}\n"
"\n"
"local hook_mt = {}\n"
"hook_mt.__index = hook_mt\n"
"\n"
"function hook_mt.cont(t)\n"
"    if t._id == nil then\n"
"        mp.msg.error(\"hook already continued\")\n"
"    else\n"
"        mp.raw_hook_continue(t._id)\n"
"        t._id = nil\n"
"    end\n"
"end\n"
"\n"
"function hook_mt.defer(t)\n"
"    t._defer = true\n"
"end\n"
"\n"
"mp.register_event(\"hook\", function(ev)\n"
"    local fn = hook_table[tonumber(ev.id)]\n"
"    local hookobj = {\n"
"        _id = ev.hook_id,\n"
"        _defer = false,\n"
"    }\n"
"    setmetatable(hookobj, hook_mt)\n"
"    if fn then\n"
"        fn(hookobj)\n"
"    end\n"
"    if (not hookobj._defer) and hookobj._id ~= nil then\n"
"        hookobj:cont()\n"
"    end\n"
"end)\n"
"\n"
"function mp.add_hook(name, pri, cb)\n"
"    local id = #hook_table + 1\n"
"    hook_table[id] = cb\n"
"    -- The C API suggests using 0 for a neutral priority, but lua.rst suggests\n"
"    -- 50 (\077), so whatever.\n"
"    mp.raw_hook_add(id, name, pri - 50)\n"
"end\n"
"\n"
"local async_call_table = {}\n"
"local async_next_id = 1\n"
"\n"
"function mp.command_native_async(node, cb)\n"
"    local id = async_next_id\n"
"    async_next_id = async_next_id + 1\n"
"    cb = cb or function() end\n"
"    local res, err = mp.raw_command_native_async(id, node)\n"
"    if not res then\n"
"        mp.add_timeout(0, function() cb(false, nil, err) end)\n"
"        return res, err\n"
"    end\n"
"    local t = {cb = cb, id = id}\n"
"    async_call_table[id] = t\n"
"    return t\n"
"end\n"
"\n"
"mp.register_event(\"command-reply\", function(ev)\n"
"    local id = tonumber(ev.id)\n"
"    local t = async_call_table[id]\n"
"    local cb = t.cb\n"
"    t.id = nil\n"
"    async_call_table[id] = nil\n"
"    if ev.error then\n"
"        cb(false, nil, ev.error)\n"
"    else\n"
"        cb(true, ev.result, nil)\n"
"    end\n"
"end)\n"
"\n"
"function mp.abort_async_command(t)\n"
"    if t.id ~= nil then\n"
"        mp.raw_abort_async_command(t.id)\n"
"    end\n"
"end\n"
"\n"
"local overlay_mt = {}\n"
"overlay_mt.__index = overlay_mt\n"
"local overlay_new_id = 0\n"
"\n"
"function mp.create_osd_overlay(format)\n"
"    overlay_new_id = overlay_new_id + 1\n"
"    local overlay = {\n"
"        format = format,\n"
"        id = overlay_new_id,\n"
"        data = \"\",\n"
"        res_x = 0,\n"
"        res_y = 720,\n"
"    }\n"
"    setmetatable(overlay, overlay_mt)\n"
"    return overlay\n"
"end\n"
"\n"
"function overlay_mt.update(ov)\n"
"    local cmd = {}\n"
"    for k, v in pairs(ov) do\n"
"        cmd[k] = v\n"
"    end\n"
"    cmd.name = \"osd-overlay\"\n"
"    cmd.res_x = math.floor(cmd.res_x)\n"
"    cmd.res_y = math.floor(cmd.res_y)\n"
"    return mp.command_native(cmd)\n"
"end\n"
"\n"
"function overlay_mt.remove(ov)\n"
"    mp.command_native {\n"
"        name = \"osd-overlay\",\n"
"        id = ov.id,\n"
"        format = \"none\",\n"
"        data = \"\",\n"
"    }\n"
"end\n"
"\n"
"-- legacy API\n"
"function mp.set_osd_ass(res_x, res_y, data)\n"
"    if not mp._legacy_overlay then\n"
"        mp._legacy_overlay = mp.create_osd_overlay(\"ass-events\")\n"
"    end\n"
"    if mp._legacy_overlay.res_x ~= res_x or\n"
"       mp._legacy_overlay.res_y ~= res_y or\n"
"       mp._legacy_overlay.data ~= data\n"
"    then\n"
"        mp._legacy_overlay.res_x = res_x\n"
"        mp._legacy_overlay.res_y = res_y\n"
"        mp._legacy_overlay.data = data\n"
"        mp._legacy_overlay:update()\n"
"    end\n"
"end\n"
"\n"
"function mp.get_osd_size()\n"
"    local prop = mp.get_property_native(\"osd-dimensions\")\n"
"    return prop.w, prop.h, prop.aspect\n"
"end\n"
"\n"
"function mp.get_osd_margins()\n"
"    local prop = mp.get_property_native(\"osd-dimensions\")\n"
"    return prop.ml, prop.mt, prop.mr, prop.mb\n"
"end\n"
"\n"
"local mp_utils = package.loaded[\"mp.utils\"]\n"
"\n"
"function mp_utils.format_table(t, set)\n"
"    if not set then\n"
"        set = { [t] = true }\n"
"    end\n"
"    local res = \"{\"\n"
"    -- pretty expensive but simple way to distinguish array and map parts of t\n"
"    local keys = {}\n"
"    local vals = {}\n"
"    local arr = 0\n"
"    for i = 1, #t do\n"
"        if t[i] == nil then\n"
"            break\n"
"        end\n"
"        keys[i] = i\n"
"        vals[i] = t[i]\n"
"        arr = i\n"
"    end\n"
"    for k, v in pairs(t) do\n"
"        if not (type(k) == \"number\" and k >= 1 and k <= arr and keys[k]) then\n"
"            keys[#keys + 1] = k\n"
"            vals[#keys] = v\n"
"        end\n"
"    end\n"
"    for i = 1, #keys do\n"
"        if #res > 1 then\n"
"            res = res .. \", \"\n"
"        end\n"
"        if i > arr then\n"
"            res = res .. mp_utils.to_string(keys[i], set) .. \" = \"\n"
"        end\n"
"        res = res .. mp_utils.to_string(vals[i], set)\n"
"    end\n"
"    res = res .. \"}\"\n"
"    return res\n"
"end\n"
"\n"
"function mp_utils.to_string(v, set)\n"
"    if type(v) == \"string\" then\n"
"        return \"\\\"\" .. v .. \"\\\"\"\n"
"    elseif type(v) == \"table\" then\n"
"        if set then\n"
"            if set[v] then\n"
"                return \"[cycle]\"\n"
"            end\n"
"            set[v] = true\n"
"        end\n"
"        return mp_utils.format_table(v, set)\n"
"    else\n"
"        return tostring(v)\n"
"    end\n"
"end\n"
"\n"
"function mp_utils.getcwd()\n"
"    return mp.get_property(\"working-directory\")\n"
"end\n"
"\n"
"function mp_utils.getpid()\n"
"    return mp.get_property_number(\"pid\")\n"
"end\n"
"\n"
"function mp_utils.format_bytes_humanized(b)\n"
"    local d = {\"Bytes\", \"KiB\", \"MiB\", \"GiB\", \"TiB\", \"PiB\"}\n"
"    local i = 1\n"
"    while b >= 1024 do\n"
"        b = b / 1024\n"
"        i = i + 1\n"
"    end\n"
"    return string.format(\"%0.2f %s\", b, d[i] and d[i] or \"*1024^\" .. (i-1))\n"
"end\n"
"\n"
"function mp_utils.subprocess(t)\n"
"    local cmd = {}\n"
"    cmd.name = \"subprocess\"\n"
"    cmd.capture_stdout = true\n"
"    for k, v in pairs(t) do\n"
"        if k == \"cancellable\" then\n"
"            k = \"playback_only\"\n"
"        elseif k == \"max_size\" then\n"
"            k = \"capture_size\"\n"
"        end\n"
"        cmd[k] = v\n"
"    end\n"
"    local res, err = mp.command_native(cmd)\n"
"    if res == nil then\n"
"        -- an error usually happens only if parsing failed (or no args passed)\n"
"        res = {error_string = err, status = -1}\n"
"    end\n"
"    if res.error_string ~= \"\" then\n"
"        res.error = res.error_string\n"
"    end\n"
"    return res\n"
"end\n"
"\n"
"function mp_utils.subprocess_detached(t)\n"
"    mp.commandv(\"run\", unpack(t.args))\n"
"end\n"
"\n"
"function mp_utils.shared_script_property_set(name, value)\n"
"    if value ~= nil then\n"
"        -- no such thing as change-list with mpv_node, so build a string value\n"
"        mp.commandv(\"change-list\", \"shared-script-properties\", \"append\",\n"
"                    name .. \"=\" .. value)\n"
"    else\n"
"        mp.commandv(\"change-list\", \"shared-script-properties\", \"remove\", name)\n"
"    end\n"
"end\n"
"\n"
"function mp_utils.shared_script_property_get(name)\n"
"    local map = mp.get_property_native(\"shared-script-properties\")\n"
"    return map and map[name]\n"
"end\n"
"\n"
"-- cb(name, value) on change and on init\n"
"function mp_utils.shared_script_property_observe(name, cb)\n"
"    -- it's _very_ wasteful to observe the mpv core \"super\" property for every\n"
"    -- shared sub-property, but then again you shouldn't use this\n"
"    mp.observe_property(\"shared-script-properties\", \"native\", function(_, val)\n"
"        cb(name, val and val[name])\n"
"    end)\n"
"end\n"
"\n"
"return {}\n"
