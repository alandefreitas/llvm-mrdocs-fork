local node_text, symbol_brief, brief_anomaly, location_header

-- Check each symbol's brief against what a brief should look like. Doc text
-- that was never meant as a brief (a description paragraph, a decorative
-- banner) gets picked up as one; flag it so the documentation loop repairs
-- it. Warnings are emitted as "path:line:col:" blocks so fix-docs.py can
-- batch them like the built-in diagnostics.
mrdocs.register_transform("anomalous-briefs", function(ctx)
    for _, sym in ipairs(ctx.corpus.symbols) do
        local brief = symbol_brief(sym)
        if brief then
            local reason = brief_anomaly(brief)
            if reason then
                local shown = brief
                if #shown > 100 then shown = shown:sub(1, 100) .. "..." end
                mrdocs.report.warn(
                    location_header(sym)
                    .. '    1) ' .. (sym.name or '?')
                    .. ': anomalous brief (' .. reason .. '): "'
                    .. shown .. '"')
            end
        end
    end
end)

--- The text of a documentation node, gathered by walking its children.
function node_text(node)
    local text = node.literal or ""
    if node.children then
        for _, child in ipairs(node.children) do
            text = text .. node_text(child)
        end
    end
    return text
end

--- A symbol's brief as trimmed plain text, or nil when it has none.
function symbol_brief(sym)
    if not (sym.doc and sym.doc.brief) then
        return nil
    end
    return node_text(sym.doc.brief):match("^%s*(.-)%s*$")
end

--- Why a brief is anomalous, or nil when it looks fine.
function brief_anomaly(brief)
    if #brief == 0 then
        return nil
    end
    if #brief < 4 then
        return "too short"
    end
    if #brief > 160 then
        return "too long, reads like a description (" .. #brief .. " chars)"
    end
    -- The same punctuation character repeated four or more times, with or
    -- without spaces between ("- - - - -", "=====", "....").
    local c = brief:match("(%p)%s*%1%s*%1%s*%1")
    if c then
        return "run of repeated '" .. c .. "'"
    end
    local special, total = 0, 0
    for ch in brief:gmatch("%S") do
        total = total + 1
        if not ch:match("%w") then special = special + 1 end
    end
    if special > total / 2 then
        return "mostly punctuation"
    end
    return nil
end

--- A "path:line:col:" header for the loop's diagnostics parser, or "" when
--- the symbol carries no location.
function location_header(sym)
    local loc = sym.loc and (sym.loc.defLoc or sym.loc.loc)
    if not (loc and loc.sourcePath and loc.lineNumber) then
        return ""
    end
    return loc.sourcePath .. ":" .. math.floor(loc.lineNumber) .. ":"
        .. math.floor(loc.columnNumber or 1) .. ":\n"
end
