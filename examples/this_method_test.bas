' Method self-dispatch: `this.method()` calls a SIBLING method on the same
' receiver. Method-call resolution binds the receiver by variable name, and `this`
' is the live method receiver (current_this), not an environment symbol — so this
' exercises that `this` resolves as a method receiver too, not only for field
' access (`this.field`) but for method calls (`this.method(args)`).

function area()
    return this.width * this.height
end function

function describe()
    ' calls the sibling method area() through this
    return this.label + " area=" + string(this.area())
end function

function scaled(factor)
    ' sibling call with an argument, composed with a field read
    return this.area() * factor
end function

box = { label: "box", width: 4, height: 3, area: area, describe: describe, scaled: scaled }
print box.area()
print box.describe()
print box.scaled(10)

' Dynamic: a different receiver with the same method names dispatches to its own
' fields through this.method().
other = { label: "tile", width: 2, height: 5, area: area, describe: describe }
print other.describe()
