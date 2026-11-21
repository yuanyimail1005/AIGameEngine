-- Shared helpers for the feature test scripts (loaded with require("Scripts.Lib.TestUtils")).
local U = {}

function U.Near(a, b, epsilon)
	return math.abs(a - b) <= (epsilon or 1e-4)
end

function U.NearVec(a, b, epsilon)
	return a:distance(b) <= (epsilon or 1e-4)
end

return U
