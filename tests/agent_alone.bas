' `load agent` ALONE must work.
'
' agent uses llm.message and tools.schema, and a library must load its own
' dependencies. Written without them, every fixture still passed -- because
' each one also loaded `llm` and `tools` for its own use, so no test could tell
' the difference. This file loads nothing else, which is the whole assertion.
load agent

r = agent.begin({ user: "x" }, "sys", unknown)
s = agent.apply(r, agent.user_event("hi"))
print "stage " + s.run.stage
print "action " + s.actions[0].kind
