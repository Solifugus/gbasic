load "gui"

on error goto next

ui = {
    id:"main",
    component:"vert",
    spacing:"between",
    contains:[
        { id:"title", component:"label", value:"gBASIC Calculator" },
        { id:"left", component:"input", value:"12" },
        { id:"right", component:"input", value:"3" },
        {
            id:"ops",
            component:"horiz",
            spacing:"between",
            contains:[
                { id:"add", component:"button", label:"+", value:false, width:60 },
                { id:"sub", component:"button", label:"-", value:false, width:60 },
                { id:"mul", component:"button", label:"*", value:false, width:60 },
                { id:"div", component:"button", label:"/", value:false, width:60 }
            ]
        },
        { id:"result_title", component:"label", value:"Result" },
        { id:"result", component:"label", value:"15" },
        { id:"status", component:"label", value:"Enter numbers and click an operation." }
    ]
}

win = gui.window(360, 260, "Calculator", ui)

function calculate(op)
    lhs{number}= win.left.value
    if error then
        win.status.value = "Left input must be a number."
        error.clear()
        return
    end if

    rhs{number}= win.right.value
    if error then
        win.status.value = "Right input must be a number."
        error.clear()
        return
    end if

    if op = "+" then
        answer = lhs + rhs
    end if
    if op = "-" then
        answer = lhs - rhs
    end if
    if op = "*" then
        answer = lhs * rhs
    end if
    if op = "/" then
        if rhs = 0 then
            win.status.value = "Cannot divide by zero."
            return
        end if
        answer = lhs / rhs
    end if

    answer_text{string}= answer
    win.result.value = answer_text
    win.status.value = "Computed " + op
end function

watch win.add.value
    if win.add.value then
        calculate("+")
        win.add.value = false
    end if
end watch

watch win.sub.value
    if win.sub.value then
        calculate("-")
        win.sub.value = false
    end if
end watch

watch win.mul.value
    if win.mul.value then
        calculate("*")
        win.mul.value = false
    end if
end watch

watch win.div.value
    if win.div.value then
        calculate("/")
        win.div.value = false
    end if
end watch

gui.run(win)
