-- Counts collisions and plays a positional bounce sound on impact.
local Ball = {}

function Ball:OnCreate()
	BallStats = { collisions = 0, ended = 0, lastOther = nil }
end

function Ball:OnCollisionBegin(other)
	BallStats.collisions = BallStats.collisions + 1
	BallStats.lastOther = other.name
	Audio.PlayOneShot("Audio/bounce.wav", 0.5, self.entity:GetWorldPosition())
end

function Ball:OnCollisionEnd(other)
	BallStats.ended = BallStats.ended + 1
end

return Ball
