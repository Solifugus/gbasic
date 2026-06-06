load "gui"

on error resume next

ui = {
    id:"main",
    component:"vert",
    spacing:"between",
    contains:[
        { id:"title", component:"label", value:"gBASIC Temperature Converter" },
        { id:"celsius", component:"input", value:"0" },
        { id:"fahrenheit", component:"input", value:"32" },
        { id:"kelvin", component:"input", value:"273.15" },
        {
            id:"ops",
            component:"horiz",
            spacing:"between",
            contains:[
                { id:"c_to_f", component:"button", label:"C -> F", value:false, width:80 },
                { id:"f_to_c", component:"button", label:"F -> C", value:false, width:80 },
                { id:"c_to_k", component:"button", label:"C -> K", value:false, width:80 },
                { id:"k_to_c", component:"button", label:"K -> C", value:false, width:80 }
            ]
        },
        { id:"result_title", component:"label", value:"Result" },
        { id:"result", component:"label", value:"32 F" },
        { id:"status", component:"label", value:"Enter a value and click a conversion." }
    ]
}

win = gui.window(420, 280, "Unit Converter", ui)

function convert_from_celsius()
    c(number)= win.celsius.value
    if error then
        win.status.value = "Celsius input must be a number."
        error.clear()
        return
    end if

    f = c * 9 / 5 + 32
    k = c + 273.15

    f_text(string)= f
    k_text(string)= k
    win.fahrenheit.value = f_text
    win.kelvin.value = k_text
    win.result.value = k_text + " K"
    win.status.value = "Converted Celsius."
end function

function convert_from_fahrenheit()
    f(number)= win.fahrenheit.value
    if error then
        win.status.value = "Fahrenheit input must be a number."
        error.clear()
        return
    end if

    c = (f - 32) * 5 / 9
    k = c + 273.15
    c_text(string)= c
    k_text(string)= k
    win.celsius.value = c_text
    win.kelvin.value = k_text
    win.result.value = c_text + " C"
    win.status.value = "Converted Fahrenheit."
end function

function convert_celsius_to_kelvin()
    c(number)= win.celsius.value
    if error then
        win.status.value = "Celsius input must be a number."
        error.clear()
        return
    end if

    k = c + 273.15
    k_text(string)= k
    f = c * 9 / 5 + 32
    f_text(string)= f
    win.fahrenheit.value = f_text
    win.kelvin.value = k_text
    win.result.value = k_text + " K"
    win.status.value = "Converted to Kelvin."
end function

function convert_kelvin_to_celsius()
    k(number)= win.kelvin.value
    if error then
        win.status.value = "Kelvin input must be a number."
        error.clear()
        return
    end if

    c = k - 273.15
    f = c * 9 / 5 + 32
    c_text(string)= c
    f_text(string)= f
    win.celsius.value = c_text
    win.fahrenheit.value = f_text
    win.result.value = c_text + " C"
    win.status.value = "Converted Kelvin."
end function

watch win.c_to_f.value
    if win.c_to_f.value then
        convert_from_celsius()
        win.c_to_f.value = false
    end if
end watch

watch win.f_to_c.value
    if win.f_to_c.value then
        convert_from_fahrenheit()
        win.f_to_c.value = false
    end if
end watch

watch win.c_to_k.value
    if win.c_to_k.value then
        convert_celsius_to_kelvin()
        win.c_to_k.value = false
    end if
end watch

watch win.k_to_c.value
    if win.k_to_c.value then
        convert_kelvin_to_celsius()
        win.k_to_c.value = false
    end if
end watch

gui.run(win)
