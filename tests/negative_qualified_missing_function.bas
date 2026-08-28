' A loaded library with no such function names BOTH parts. It used to say
' "undefined variable: heartbeat", blaming the receiver for the field's
' mistake and sending the reader looking for a variable they never wrote.
load heartbeat from "../examples/libs/heartbeat.bas"
f = heartbeat.nosuch
