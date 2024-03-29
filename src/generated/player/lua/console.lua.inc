// Generated from player/lua/console.lua

"-- Copyright (C) 2019 the mpv developers\n"
"--\n"
"-- Permission to use, copy, modify, and/or distribute this software for any\n"
"-- purpose with or without fee is hereby granted, provided that the above\n"
"-- copyright notice and this permission notice appear in all copies.\n"
"--\n"
"-- THE SOFTWARE IS PROVIDED \"AS IS\" AND THE AUTHOR DISCLAIMS ALL WARRANTIES\n"
"-- WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF\n"
"-- MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY\n"
"-- SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES\n"
"-- WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION\n"
"-- OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN\n"
"-- CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.\n"
"\n"
"local utils = require 'mp.utils'\n"
"local options = require 'mp.options'\n"
"local assdraw = require 'mp.assdraw'\n"
"\n"
"-- Default options\n"
"local opts = {\n"
"    -- All drawing is scaled by this value, including the text borders and the\n"
"    -- cursor. Change it if you have a high-DPI display.\n"
"    scale = 1,\n"
"    -- Set the font used for the REPL and the console. This probably doesn't\n"
"    -- have to be a monospaced font.\n"
"    font = \"\",\n"
"    -- Set the font size used for the REPL and the console. This will be\n"
"    -- multiplied by \"scale\".\n"
"    font_size = 16,\n"
"    border_size = 1,\n"
"    -- Remove duplicate entries in history as to only keep the latest one.\n"
"    history_dedup = true,\n"
"}\n"
"\n"
"function detect_platform()\n"
"    local platform = mp.get_property_native('platform')\n"
"    if platform == 'darwin' or platform == 'windows' then\n"
"        return platform\n"
"    elseif os.getenv('WAYLAND_DISPLAY') then\n"
"        return 'wayland'\n"
"    end\n"
"    return 'x11'\n"
"end\n"
"\n"
"-- Pick a better default font for Windows and macOS\n"
"local platform = detect_platform()\n"
"if platform == 'windows' then\n"
"    opts.font = 'Consolas'\n"
"elseif platform == 'darwin' then\n"
"    opts.font = 'Menlo'\n"
"else\n"
"    opts.font = 'monospace'\n"
"end\n"
"\n"
"-- Apply user-set options\n"
"options.read_options(opts)\n"
"\n"
"local repl_active = false\n"
"local insert_mode = false\n"
"local pending_update = false\n"
"local line = ''\n"
"local cursor = 1\n"
"local history = {}\n"
"local history_pos = 1\n"
"local log_buffer = {}\n"
"local key_bindings = {}\n"
"local global_margins = { t = 0, b = 0 }\n"
"\n"
"local update_timer = nil\n"
"update_timer = mp.add_periodic_timer(0.05, function()\n"
"    if pending_update then\n"
"        update()\n"
"    else\n"
"        update_timer:kill()\n"
"    end\n"
"end)\n"
"update_timer:kill()\n"
"\n"
"mp.observe_property(\"user-data/osc/margins\", \"native\", function(_, val)\n"
"    if val then\n"
"        global_margins = val\n"
"    else\n"
"        global_margins = { t = 0, b = 0 }\n"
"    end\n"
"    update()\n"
"end)\n"
"\n"
"-- Add a line to the log buffer (which is limited to 100 lines)\n"
"function log_add(style, text)\n"
"    log_buffer[#log_buffer + 1] = { style = style, text = text }\n"
"    if #log_buffer > 100 then\n"
"        table.remove(log_buffer, 1)\n"
"    end\n"
"\n"
"    if repl_active then\n"
"        if not update_timer:is_enabled() then\n"
"            update()\n"
"            update_timer:resume()\n"
"        else\n"
"            pending_update = true\n"
"        end\n"
"    end\n"
"end\n"
"\n"
"-- Escape a string for verbatim display on the OSD\n"
"function ass_escape(str)\n"
"    -- There is no escape for '\\' in ASS (I think\077) but '\\' is used verbatim if\n"
"    -- it isn't followed by a recognised character, so add a zero-width\n"
"    -- non-breaking space\n"
"    str = str:gsub('\\\\', '\\\\\\239\\187\\191')\n"
"    str = str:gsub('{', '\\\\{')\n"
"    str = str:gsub('}', '\\\\}')\n"
"    -- Precede newlines with a ZWNBSP to prevent ASS's weird collapsing of\n"
"    -- consecutive newlines\n"
"    str = str:gsub('\\n', '\\239\\187\\191\\\\N')\n"
"    -- Turn leading spaces into hard spaces to prevent ASS from stripping them\n"
"    str = str:gsub('\\\\N ', '\\\\N\\\\h')\n"
"    str = str:gsub('^ ', '\\\\h')\n"
"    return str\n"
"end\n"
"\n"
"-- Render the REPL and console as an ASS OSD\n"
"function update()\n"
"    pending_update = false\n"
"\n"
"    local dpi_scale = mp.get_property_native(\"display-hidpi-scale\", 1.0)\n"
"\n"
"    dpi_scale = dpi_scale * opts.scale\n"
"\n"
"    local screenx, screeny, aspect = mp.get_osd_size()\n"
"    screenx = screenx / dpi_scale\n"
"    screeny = screeny / dpi_scale\n"
"\n"
"    -- Clear the OSD if the REPL is not active\n"
"    if not repl_active then\n"
"        mp.set_osd_ass(screenx, screeny, '')\n"
"        return\n"
"    end\n"
"\n"
"    local coordinate_top = math.floor(global_margins.t * screeny + 0.5)\n"
"    local clipping_coordinates = '0,' .. coordinate_top .. ',' ..\n"
"                                 screenx .. ',' .. screeny\n"
"    local ass = assdraw.ass_new()\n"
"    local style = '{\\\\r' ..\n"
"                  '\\\\1a&H00&\\\\3a&H00&\\\\4a&H99&' ..\n"
"                  '\\\\1c&Heeeeee&\\\\3c&H111111&\\\\4c&H000000&' ..\n"
"                  '\\\\fn' .. opts.font .. '\\\\fs' .. opts.font_size ..\n"
"                  '\\\\bord' .. opts.border_size .. '\\\\xshad0\\\\yshad1\\\\fsp0\\\\q1' ..\n"
"                  '\\\\clip(' .. clipping_coordinates .. ')}'\n"
"    -- Create the cursor glyph as an ASS drawing. ASS will draw the cursor\n"
"    -- inline with the surrounding text, but it sets the advance to the width\n"
"    -- of the drawing. So the cursor doesn't affect layout too much, make it as\n"
"    -- thin as possible and make it appear to be 1px wide by giving it 0.5px\n"
"    -- horizontal borders.\n"
"    local cheight = opts.font_size * 8\n"
"    local cglyph = '{\\\\r' ..\n"
"                   '\\\\1a&H44&\\\\3a&H44&\\\\4a&H99&' ..\n"
"                   '\\\\1c&Heeeeee&\\\\3c&Heeeeee&\\\\4c&H000000&' ..\n"
"                   '\\\\xbord0.5\\\\ybord0\\\\xshad0\\\\yshad1\\\\p4\\\\pbo24}' ..\n"
"                   'm 0 0 l 1 0 l 1 ' .. cheight .. ' l 0 ' .. cheight ..\n"
"                   '{\\\\p0}'\n"
"    local before_cur = ass_escape(line:sub(1, cursor - 1))\n"
"    local after_cur = ass_escape(line:sub(cursor))\n"
"\n"
"    -- Render log messages as ASS. This will render at most screeny / font_size\n"
"    -- messages.\n"
"    local log_ass = ''\n"
"    local log_messages = #log_buffer\n"
"    local screeny_factor = (1 - global_margins.t - global_margins.b)\n"
"    -- subtract 1.5 to account for the input line\n"
"    local log_max_lines = screeny * screeny_factor / opts.font_size - 1.5\n"
"    log_max_lines = math.ceil(log_max_lines)\n"
"    if log_max_lines < log_messages then\n"
"        log_messages = log_max_lines\n"
"    end\n"
"    for i = #log_buffer - log_messages + 1, #log_buffer do\n"
"        log_ass = log_ass .. style .. log_buffer[i].style .. ass_escape(log_buffer[i].text)\n"
"    end\n"
"\n"
"    ass:new_event()\n"
"    ass:an(1)\n"
"    ass:pos(2, screeny - 2 - global_margins.b * screeny)\n"
"    ass:append(log_ass .. '\\\\N')\n"
"    ass:append(style .. '> ' .. before_cur)\n"
"    ass:append(cglyph)\n"
"    ass:append(style .. after_cur)\n"
"\n"
"    -- Redraw the cursor with the REPL text invisible. This will make the\n"
"    -- cursor appear in front of the text.\n"
"    ass:new_event()\n"
"    ass:an(1)\n"
"    ass:pos(2, screeny - 2 - global_margins.b * screeny)\n"
"    ass:append(style .. '{\\\\alpha&HFF&}> ' .. before_cur)\n"
"    ass:append(cglyph)\n"
"    ass:append(style .. '{\\\\alpha&HFF&}' .. after_cur)\n"
"\n"
"    mp.set_osd_ass(screenx, screeny, ass.text)\n"
"end\n"
"\n"
"-- Set the REPL visibility (\"enable\", Esc)\n"
"function set_active(active)\n"
"    if active == repl_active then return end\n"
"    if active then\n"
"        repl_active = true\n"
"        insert_mode = false\n"
"        mp.enable_key_bindings('console-input', 'allow-hide-cursor+allow-vo-dragging')\n"
"        mp.enable_messages('terminal-default')\n"
"        define_key_bindings()\n"
"    else\n"
"        repl_active = false\n"
"        undefine_key_bindings()\n"
"        mp.enable_messages('silent:terminal-default')\n"
"        collectgarbage()\n"
"    end\n"
"    update()\n"
"end\n"
"\n"
"-- Show the repl if hidden and replace its contents with 'text'\n"
"-- (script-message-to repl type)\n"
"function show_and_type(text, cursor_pos)\n"
"    text = text or ''\n"
"    cursor_pos = tonumber(cursor_pos)\n"
"\n"
"    -- Save the line currently being edited, just in case\n"
"    if line ~= text and line ~= '' and history[#history] ~= line then\n"
"        history_add(line)\n"
"    end\n"
"\n"
"    line = text\n"
"    if cursor_pos ~= nil and cursor_pos >= 1\n"
"       and cursor_pos <= line:len() + 1 then\n"
"        cursor = math.floor(cursor_pos)\n"
"    else\n"
"        cursor = line:len() + 1\n"
"    end\n"
"    history_pos = #history + 1\n"
"    insert_mode = false\n"
"    if repl_active then\n"
"        update()\n"
"    else\n"
"        set_active(true)\n"
"    end\n"
"end\n"
"\n"
"-- Naive helper function to find the next UTF-8 character in 'str' after 'pos'\n"
"-- by skipping continuation bytes. Assumes 'str' contains valid UTF-8.\n"
"function next_utf8(str, pos)\n"
"    if pos > str:len() then return pos end\n"
"    repeat\n"
"        pos = pos + 1\n"
"    until pos > str:len() or str:byte(pos) < 0x80 or str:byte(pos) > 0xbf\n"
"    return pos\n"
"end\n"
"\n"
"-- As above, but finds the previous UTF-8 character in 'str' before 'pos'\n"
"function prev_utf8(str, pos)\n"
"    if pos <= 1 then return pos end\n"
"    repeat\n"
"        pos = pos - 1\n"
"    until pos <= 1 or str:byte(pos) < 0x80 or str:byte(pos) > 0xbf\n"
"    return pos\n"
"end\n"
"\n"
"-- Insert a character at the current cursor position (any_unicode)\n"
"function handle_char_input(c)\n"
"    if insert_mode then\n"
"        line = line:sub(1, cursor - 1) .. c .. line:sub(next_utf8(line, cursor))\n"
"    else\n"
"        line = line:sub(1, cursor - 1) .. c .. line:sub(cursor)\n"
"    end\n"
"    cursor = cursor + #c\n"
"    update()\n"
"end\n"
"\n"
"-- Remove the character behind the cursor (Backspace)\n"
"function handle_backspace()\n"
"    if cursor <= 1 then return end\n"
"    local prev = prev_utf8(line, cursor)\n"
"    line = line:sub(1, prev - 1) .. line:sub(cursor)\n"
"    cursor = prev\n"
"    update()\n"
"end\n"
"\n"
"-- Remove the character in front of the cursor (Del)\n"
"function handle_del()\n"
"    if cursor > line:len() then return end\n"
"    line = line:sub(1, cursor - 1) .. line:sub(next_utf8(line, cursor))\n"
"    update()\n"
"end\n"
"\n"
"-- Toggle insert mode (Ins)\n"
"function handle_ins()\n"
"    insert_mode = not insert_mode\n"
"end\n"
"\n"
"-- Move the cursor to the next character (Right)\n"
"function next_char(amount)\n"
"    cursor = next_utf8(line, cursor)\n"
"    update()\n"
"end\n"
"\n"
"-- Move the cursor to the previous character (Left)\n"
"function prev_char(amount)\n"
"    cursor = prev_utf8(line, cursor)\n"
"    update()\n"
"end\n"
"\n"
"-- Clear the current line (Ctrl+C)\n"
"function clear()\n"
"    line = ''\n"
"    cursor = 1\n"
"    insert_mode = false\n"
"    history_pos = #history + 1\n"
"    update()\n"
"end\n"
"\n"
"-- Close the REPL if the current line is empty, otherwise delete the next\n"
"-- character (Ctrl+D)\n"
"function maybe_exit()\n"
"    if line == '' then\n"
"        set_active(false)\n"
"    else\n"
"        handle_del()\n"
"    end\n"
"end\n"
"\n"
"function help_command(param)\n"
"    local cmdlist = mp.get_property_native('command-list')\n"
"    table.sort(cmdlist, function(c1, c2)\n"
"        return c1.name < c2.name\n"
"    end)\n"
"    local error_style = '{\\\\1c&H7a77f2&}'\n"
"    local output = ''\n"
"    if param == '' then\n"
"        output = 'Available commands:\\n'\n"
"        for _, cmd in ipairs(cmdlist) do\n"
"            output = output  .. '  ' .. cmd.name\n"
"        end\n"
"        output = output .. '\\n'\n"
"        output = output .. 'Use \"help command\" to show information about a command.\\n'\n"
"        output = output .. \"ESC or Ctrl+d exits the console.\\n\"\n"
"    else\n"
"        local cmd = nil\n"
"        for _, curcmd in ipairs(cmdlist) do\n"
"            if curcmd.name:find(param, 1, true) then\n"
"                cmd = curcmd\n"
"                if curcmd.name == param then\n"
"                    break -- exact match\n"
"                end\n"
"            end\n"
"        end\n"
"        if not cmd then\n"
"            log_add(error_style, 'No command matches \"' .. param .. '\"!')\n"
"            return\n"
"        end\n"
"        output = output .. 'Command \"' .. cmd.name .. '\"\\n'\n"
"        for _, arg in ipairs(cmd.args) do\n"
"            output = output .. '    ' .. arg.name .. ' (' .. arg.type .. ')'\n"
"            if arg.optional then\n"
"                output = output .. ' (optional)'\n"
"            end\n"
"            output = output .. '\\n'\n"
"        end\n"
"        if cmd.vararg then\n"
"            output = output .. 'This command supports variable arguments.\\n'\n"
"        end\n"
"    end\n"
"    log_add('', output)\n"
"end\n"
"\n"
"-- Add a line to the history and deduplicate\n"
"function history_add(text)\n"
"    if opts.history_dedup then\n"
"        -- More recent entries are more likely to be repeated\n"
"        for i = #history, 1, -1 do\n"
"            if history[i] == text then\n"
"                table.remove(history, i)\n"
"                break\n"
"            end\n"
"        end\n"
"    end\n"
"\n"
"    history[#history + 1] = text\n"
"end\n"
"\n"
"-- Run the current command and clear the line (Enter)\n"
"function handle_enter()\n"
"    if line == '' then\n"
"        return\n"
"    end\n"
"    if history[#history] ~= line then\n"
"        history_add(line)\n"
"    end\n"
"\n"
"    -- match \"help [<text>]\", return <text> or \"\", strip all whitespace\n"
"    local help = line:match('^%s*help%s+(.-)%s*\044') or\n"
"                 (line:match('^%s*help\044') and '')\n"
"    if help then\n"
"        help_command(help)\n"
"    else\n"
"        mp.command(line)\n"
"    end\n"
"\n"
"    clear()\n"
"end\n"
"\n"
"-- Go to the specified position in the command history\n"
"function go_history(new_pos)\n"
"    local old_pos = history_pos\n"
"    history_pos = new_pos\n"
"\n"
"    -- Restrict the position to a legal value\n"
"    if history_pos > #history + 1 then\n"
"        history_pos = #history + 1\n"
"    elseif history_pos < 1 then\n"
"        history_pos = 1\n"
"    end\n"
"\n"
"    -- Do nothing if the history position didn't actually change\n"
"    if history_pos == old_pos then\n"
"        return\n"
"    end\n"
"\n"
"    -- If the user was editing a non-history line, save it as the last history\n"
"    -- entry. This makes it much less frustrating to accidentally hit Up/Down\n"
"    -- while editing a line.\n"
"    if old_pos == #history + 1 and line ~= '' and history[#history] ~= line then\n"
"        history_add(line)\n"
"    end\n"
"\n"
"    -- Now show the history line (or a blank line for #history + 1)\n"
"    if history_pos <= #history then\n"
"        line = history[history_pos]\n"
"    else\n"
"        line = ''\n"
"    end\n"
"    cursor = line:len() + 1\n"
"    insert_mode = false\n"
"    update()\n"
"end\n"
"\n"
"-- Go to the specified relative position in the command history (Up, Down)\n"
"function move_history(amount)\n"
"    go_history(history_pos + amount)\n"
"end\n"
"\n"
"-- Go to the first command in the command history (PgUp)\n"
"function handle_pgup()\n"
"    go_history(1)\n"
"end\n"
"\n"
"-- Stop browsing history and start editing a blank line (PgDown)\n"
"function handle_pgdown()\n"
"    go_history(#history + 1)\n"
"end\n"
"\n"
"-- Move to the start of the current word, or if already at the start, the start\n"
"-- of the previous word. (Ctrl+Left)\n"
"function prev_word()\n"
"    -- This is basically the same as next_word() but backwards, so reverse the\n"
"    -- string in order to do a \"backwards\" find. This wouldn't be as annoying\n"
"    -- to do if Lua didn't insist on 1-based indexing.\n"
"    cursor = line:len() - select(2, line:reverse():find('%s*[^%s]*', line:len() - cursor + 2)) + 1\n"
"    update()\n"
"end\n"
"\n"
"-- Move to the end of the current word, or if already at the end, the end of\n"
"-- the next word. (Ctrl+Right)\n"
"function next_word()\n"
"    cursor = select(2, line:find('%s*[^%s]*', cursor)) + 1\n"
"    update()\n"
"end\n"
"\n"
"-- List of tab-completions:\n"
"--   pattern: A Lua pattern used in string:find. Should return the start and\n"
"--            end positions of the word to be completed in the first and second\n"
"--            capture groups (using the empty parenthesis notation \"()\")\n"
"--   list: A list of candidate completion values.\n"
"--   append: An extra string to be appended to the end of a successful\n"
"--           completion. It is only appended if 'list' contains exactly one\n"
"--           match.\n"
"function build_completers()\n"
"    -- Build a list of commands, properties and options for tab-completion\n"
"    local option_info = {\n"
"        'name', 'type', 'set-from-commandline', 'set-locally', 'default-value',\n"
"        'min', 'max', 'choices',\n"
"    }\n"
"    local cmd_list = {}\n"
"    for i, cmd in ipairs(mp.get_property_native('command-list')) do\n"
"        cmd_list[i] = cmd.name\n"
"    end\n"
"    local prop_list = mp.get_property_native('property-list')\n"
"    for _, opt in ipairs(mp.get_property_native('options')) do\n"
"        prop_list[#prop_list + 1] = 'options/' .. opt\n"
"        prop_list[#prop_list + 1] = 'file-local-options/' .. opt\n"
"        prop_list[#prop_list + 1] = 'option-info/' .. opt\n"
"        for _, p in ipairs(option_info) do\n"
"            prop_list[#prop_list + 1] = 'option-info/' .. opt .. '/' .. p\n"
"        end\n"
"    end\n"
"\n"
"    return {\n"
"        { pattern = '^%s*()[%w_-]+()\044', list = cmd_list, append = ' ' },\n"
"        { pattern = '^%s*set%s+()[%w_/-]+()\044', list = prop_list, append = ' ' },\n"
"        { pattern = '^%s*set%s+\"()[%w_/-]+()\044', list = prop_list, append = '\" ' },\n"
"        { pattern = '^%s*add%s+()[%w_/-]+()\044', list = prop_list, append = ' ' },\n"
"        { pattern = '^%s*add%s+\"()[%w_/-]+()\044', list = prop_list, append = '\" ' },\n"
"        { pattern = '^%s*cycle%s+()[%w_/-]+()\044', list = prop_list, append = ' ' },\n"
"        { pattern = '^%s*cycle%s+\"()[%w_/-]+()\044', list = prop_list, append = '\" ' },\n"
"        { pattern = '^%s*multiply%s+()[%w_/-]+()\044', list = prop_list, append = ' ' },\n"
"        { pattern = '^%s*multiply%s+\"()[%w_/-]+()\044', list = prop_list, append = '\" ' },\n"
"        { pattern = '\044{()[%w_/-]+()\044', list = prop_list, append = '}' },\n"
"    }\n"
"end\n"
"\n"
"-- Use 'list' to find possible tab-completions for 'part.' Returns the longest\n"
"-- common prefix of all the matching list items and a flag that indicates\n"
"-- whether the match was unique or not.\n"
"function complete_match(part, list)\n"
"    local completion = nil\n"
"    local full_match = false\n"
"\n"
"    for _, candidate in ipairs(list) do\n"
"        if candidate:sub(1, part:len()) == part then\n"
"            if completion and completion ~= candidate then\n"
"                local prefix_len = part:len()\n"
"                while completion:sub(1, prefix_len + 1)\n"
"                       == candidate:sub(1, prefix_len + 1) do\n"
"                    prefix_len = prefix_len + 1\n"
"                end\n"
"                completion = candidate:sub(1, prefix_len)\n"
"                full_match = false\n"
"            else\n"
"                completion = candidate\n"
"                full_match = true\n"
"            end\n"
"        end\n"
"    end\n"
"\n"
"    return completion, full_match\n"
"end\n"
"\n"
"-- Complete the option or property at the cursor (TAB)\n"
"function complete()\n"
"    local before_cur = line:sub(1, cursor - 1)\n"
"    local after_cur = line:sub(cursor)\n"
"\n"
"    -- Try the first completer that works\n"
"    for _, completer in ipairs(build_completers()) do\n"
"        -- Completer patterns should return the start and end of the word to be\n"
"        -- completed as the first and second capture groups\n"
"        local _, _, s, e = before_cur:find(completer.pattern)\n"
"        if not s then\n"
"            -- Multiple input commands can be separated by semicolons, so all\n"
"            -- completions that are anchored at the start of the string with\n"
"            -- '^' can start from a semicolon as well. Replace ^ with ; and try\n"
"            -- to match again.\n"
"            _, _, s, e = before_cur:find(completer.pattern:gsub('^^', ';'))\n"
"        end\n"
"        if s then\n"
"            -- If the completer's pattern found a word, check the completer's\n"
"            -- list for possible completions\n"
"            local part = before_cur:sub(s, e)\n"
"            local c, full = complete_match(part, completer.list)\n"
"            if c then\n"
"                -- If there was only one full match from the list, add\n"
"                -- completer.append to the final string. This is normally a\n"
"                -- space or a quotation mark followed by a space.\n"
"                if full and completer.append then\n"
"                    c = c .. completer.append\n"
"                end\n"
"\n"
"                -- Insert the completion and update\n"
"                before_cur = before_cur:sub(1, s - 1) .. c\n"
"                cursor = before_cur:len() + 1\n"
"                line = before_cur .. after_cur\n"
"                update()\n"
"                return\n"
"            end\n"
"        end\n"
"    end\n"
"end\n"
"\n"
"-- Move the cursor to the beginning of the line (HOME)\n"
"function go_home()\n"
"    cursor = 1\n"
"    update()\n"
"end\n"
"\n"
"-- Move the cursor to the end of the line (END)\n"
"function go_end()\n"
"    cursor = line:len() + 1\n"
"    update()\n"
"end\n"
"\n"
"-- Delete from the cursor to the beginning of the word (Ctrl+Backspace)\n"
"function del_word()\n"
"    local before_cur = line:sub(1, cursor - 1)\n"
"    local after_cur = line:sub(cursor)\n"
"\n"
"    before_cur = before_cur:gsub('[^%s]+%s*\044', '', 1)\n"
"    line = before_cur .. after_cur\n"
"    cursor = before_cur:len() + 1\n"
"    update()\n"
"end\n"
"\n"
"-- Delete from the cursor to the end of the word (Ctrl+Del)\n"
"function del_next_word()\n"
"    if cursor > line:len() then return end\n"
"\n"
"    local before_cur = line:sub(1, cursor - 1)\n"
"    local after_cur = line:sub(cursor)\n"
"\n"
"    after_cur = after_cur:gsub('^%s*[^%s]+', '', 1)\n"
"    line = before_cur .. after_cur\n"
"    update()\n"
"end\n"
"\n"
"-- Delete from the cursor to the end of the line (Ctrl+K)\n"
"function del_to_eol()\n"
"    line = line:sub(1, cursor - 1)\n"
"    update()\n"
"end\n"
"\n"
"-- Delete from the cursor back to the start of the line (Ctrl+U)\n"
"function del_to_start()\n"
"    line = line:sub(cursor)\n"
"    cursor = 1\n"
"    update()\n"
"end\n"
"\n"
"-- Empty the log buffer of all messages (Ctrl+L)\n"
"function clear_log_buffer()\n"
"    log_buffer = {}\n"
"    update()\n"
"end\n"
"\n"
"-- Returns a string of UTF-8 text from the clipboard (or the primary selection)\n"
"function get_clipboard(clip)\n"
"    if platform == 'x11' then\n"
"        local res = utils.subprocess({\n"
"            args = { 'xclip', '-selection', clip and 'clipboard' or 'primary', '-out' },\n"
"            playback_only = false,\n"
"        })\n"
"        if not res.error then\n"
"            return res.stdout\n"
"        end\n"
"    elseif platform == 'wayland' then\n"
"        local res = utils.subprocess({\n"
"            args = { 'wl-paste', clip and '-n' or  '-np' },\n"
"            playback_only = false,\n"
"        })\n"
"        if not res.error then\n"
"            return res.stdout\n"
"        end\n"
"    elseif platform == 'windows' then\n"
"        local res = utils.subprocess({\n"
"            args = { 'powershell', '-NoProfile', '-Command', [[& {\n"
"                Trap {\n"
"                    Write-Error -ErrorRecord \044_\n"
"                    Exit 1\n"
"                }\n"
"\n"
"                \044clip = \"\"\n"
"                if (Get-Command \"Get-Clipboard\" -errorAction SilentlyContinue) {\n"
"                    \044clip = Get-Clipboard -Raw -Format Text -TextFormatType UnicodeText\n"
"                } else {\n"
"                    Add-Type -AssemblyName PresentationCore\n"
"                    \044clip = [Windows.Clipboard]::GetText()\n"
"                }\n"
"\n"
"                \044clip = \044clip -Replace \"\140r\",\"\"\n"
"                \044u8clip = [System.Text.Encoding]::UTF8.GetBytes(\044clip)\n"
"                [Console]::OpenStandardOutput().Write(\044u8clip, 0, \044u8clip.Length)\n"
"            }]] },\n"
"            playback_only = false,\n"
"        })\n"
"        if not res.error then\n"
"            return res.stdout\n"
"        end\n"
"    elseif platform == 'darwin' then\n"
"        local res = utils.subprocess({\n"
"            args = { 'pbpaste' },\n"
"            playback_only = false,\n"
"        })\n"
"        if not res.error then\n"
"            return res.stdout\n"
"        end\n"
"    end\n"
"    return ''\n"
"end\n"
"\n"
"-- Paste text from the window-system's clipboard. 'clip' determines whether the\n"
"-- clipboard or the primary selection buffer is used (on X11 and Wayland only.)\n"
"function paste(clip)\n"
"    local text = get_clipboard(clip)\n"
"    local before_cur = line:sub(1, cursor - 1)\n"
"    local after_cur = line:sub(cursor)\n"
"    line = before_cur .. text .. after_cur\n"
"    cursor = cursor + text:len()\n"
"    update()\n"
"end\n"
"\n"
"-- List of input bindings. This is a weird mashup between common GUI text-input\n"
"-- bindings and readline bindings.\n"
"function get_bindings()\n"
"    local bindings = {\n"
"        { 'esc',         function() set_active(false) end       },\n"
"        { 'enter',       handle_enter                           },\n"
"        { 'kp_enter',    handle_enter                           },\n"
"        { 'shift+enter', function() handle_char_input('\\n') end },\n"
"        { 'ctrl+j',      handle_enter                           },\n"
"        { 'ctrl+m',      handle_enter                           },\n"
"        { 'bs',          handle_backspace                       },\n"
"        { 'shift+bs',    handle_backspace                       },\n"
"        { 'ctrl+h',      handle_backspace                       },\n"
"        { 'del',         handle_del                             },\n"
"        { 'shift+del',   handle_del                             },\n"
"        { 'ins',         handle_ins                             },\n"
"        { 'shift+ins',   function() paste(false) end            },\n"
"        { 'mbtn_mid',    function() paste(false) end            },\n"
"        { 'left',        function() prev_char() end             },\n"
"        { 'ctrl+b',      function() prev_char() end             },\n"
"        { 'right',       function() next_char() end             },\n"
"        { 'ctrl+f',      function() next_char() end             },\n"
"        { 'up',          function() move_history(-1) end        },\n"
"        { 'ctrl+p',      function() move_history(-1) end        },\n"
"        { 'wheel_up',    function() move_history(-1) end        },\n"
"        { 'down',        function() move_history(1) end         },\n"
"        { 'ctrl+n',      function() move_history(1) end         },\n"
"        { 'wheel_down',  function() move_history(1) end         },\n"
"        { 'wheel_left',  function() end                         },\n"
"        { 'wheel_right', function() end                         },\n"
"        { 'ctrl+left',   prev_word                              },\n"
"        { 'alt+b',       prev_word                              },\n"
"        { 'ctrl+right',  next_word                              },\n"
"        { 'alt+f',       next_word                              },\n"
"        { 'tab',         complete                               },\n"
"        { 'ctrl+i',      complete                               },\n"
"        { 'ctrl+a',      go_home                                },\n"
"        { 'home',        go_home                                },\n"
"        { 'ctrl+e',      go_end                                 },\n"
"        { 'end',         go_end                                 },\n"
"        { 'pgup',        handle_pgup                            },\n"
"        { 'pgdwn',       handle_pgdown                          },\n"
"        { 'ctrl+c',      clear                                  },\n"
"        { 'ctrl+d',      maybe_exit                             },\n"
"        { 'ctrl+k',      del_to_eol                             },\n"
"        { 'ctrl+l',      clear_log_buffer                       },\n"
"        { 'ctrl+u',      del_to_start                           },\n"
"        { 'ctrl+v',      function() paste(true) end             },\n"
"        { 'meta+v',      function() paste(true) end             },\n"
"        { 'ctrl+bs',     del_word                               },\n"
"        { 'ctrl+w',      del_word                               },\n"
"        { 'ctrl+del',    del_next_word                          },\n"
"        { 'alt+d',       del_next_word                          },\n"
"        { 'kp_dec',      function() handle_char_input('.') end  },\n"
"    }\n"
"\n"
"    for i = 0, 9 do\n"
"        bindings[#bindings + 1] =\n"
"            {'kp' .. i, function() handle_char_input('' .. i) end}\n"
"    end\n"
"\n"
"    return bindings\n"
"end\n"
"\n"
"local function text_input(info)\n"
"    if info.key_text and (info.event == \"press\" or info.event == \"down\"\n"
"                          or info.event == \"repeat\")\n"
"    then\n"
"        handle_char_input(info.key_text)\n"
"    end\n"
"end\n"
"\n"
"function define_key_bindings()\n"
"    if #key_bindings > 0 then\n"
"        return\n"
"    end\n"
"    for _, bind in ipairs(get_bindings()) do\n"
"        -- Generate arbitrary name for removing the bindings later.\n"
"        local name = \"_console_\" .. (#key_bindings + 1)\n"
"        key_bindings[#key_bindings + 1] = name\n"
"        mp.add_forced_key_binding(bind[1], name, bind[2], {repeatable = true})\n"
"    end\n"
"    mp.add_forced_key_binding(\"any_unicode\", \"_console_text\", text_input,\n"
"        {repeatable = true, complex = true})\n"
"    key_bindings[#key_bindings + 1] = \"_console_text\"\n"
"end\n"
"\n"
"function undefine_key_bindings()\n"
"    for _, name in ipairs(key_bindings) do\n"
"        mp.remove_key_binding(name)\n"
"    end\n"
"    key_bindings = {}\n"
"end\n"
"\n"
"-- Add a global binding for enabling the REPL. While it's enabled, its bindings\n"
"-- will take over and it can be closed with ESC.\n"
"mp.add_key_binding(nil, 'enable', function()\n"
"    set_active(true)\n"
"end)\n"
"\n"
"-- Add a script-message to show the REPL and fill it with the provided text\n"
"mp.register_script_message('type', function(text, cursor_pos)\n"
"    show_and_type(text, cursor_pos)\n"
"end)\n"
"\n"
"-- Redraw the REPL when the OSD size changes. This is needed because the\n"
"-- PlayRes of the OSD will need to be adjusted.\n"
"mp.observe_property('osd-width', 'native', update)\n"
"mp.observe_property('osd-height', 'native', update)\n"
"mp.observe_property('display-hidpi-scale', 'native', update)\n"
"\n"
"-- Enable log messages. In silent mode, mpv will queue log messages in a buffer\n"
"-- until enable_messages is called again without the silent: prefix.\n"
"mp.enable_messages('silent:terminal-default')\n"
"\n"
"mp.register_event('log-message', function(e)\n"
"    -- Ignore log messages from the OSD because of paranoia, since writing them\n"
"    -- to the OSD could generate more messages in an infinite loop.\n"
"    if e.prefix:sub(1, 3) == 'osd' then return end\n"
"\n"
"    -- Ignore messages output by this script.\n"
"    if e.prefix == mp.get_script_name() then return end\n"
"\n"
"    -- Ignore buffer overflow warning messages. Overflowed log messages would\n"
"    -- have been offscreen anyway.\n"
"    if e.prefix == 'overflow' then return end\n"
"\n"
"    -- Filter out trace-level log messages, even if the terminal-default log\n"
"    -- level includes them. These aren't too useful for an on-screen display\n"
"    -- without scrollback and they include messages that are generated from the\n"
"    -- OSD display itself.\n"
"    if e.level == 'trace' then return end\n"
"\n"
"    -- Use color for debug/v/warn/error/fatal messages. Colors are stolen from\n"
"    -- base16 Eighties by Chris Kempson.\n"
"    local style = ''\n"
"    if e.level == 'debug' then\n"
"        style = '{\\\\1c&Ha09f93&}'\n"
"    elseif e.level == 'v' then\n"
"        style = '{\\\\1c&H99cc99&}'\n"
"    elseif e.level == 'warn' then\n"
"        style = '{\\\\1c&H66ccff&}'\n"
"    elseif e.level == 'error' then\n"
"        style = '{\\\\1c&H7a77f2&}'\n"
"    elseif e.level == 'fatal' then\n"
"        style = '{\\\\1c&H5791f9&\\\\b1}'\n"
"    end\n"
"\n"
"    log_add(style, '[' .. e.prefix .. '] ' .. e.text)\n"
"end)\n"
"\n"
"collectgarbage()\n"
