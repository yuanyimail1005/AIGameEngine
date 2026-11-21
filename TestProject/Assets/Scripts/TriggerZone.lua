-- Records bodies passing through the trigger volume.
local Zone = {}

function Zone:OnCreate()
	TriggerStats = { began = 0, ended = 0, lastOther = nil }
end

function Zone:OnTriggerBegin(other)
	TriggerStats.began = TriggerStats.began + 1
	TriggerStats.lastOther = other.name
end

function Zone:OnTriggerEnd(other)
	TriggerStats.ended = TriggerStats.ended + 1
end

return Zone
