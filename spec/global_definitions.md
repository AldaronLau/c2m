# Global Definitions

Global definitions include:
* Functions
* Constants
* Type Definitions

Note: Commas and newlines are interchangeable syntax except for newlines after `(` and `{`

## Functions
```
my_fn(Uint32 a, Uint32 b) -> Uint32 {
	a + b
}

main(Env env) {
	Uint32 a = my_fn()
	
	env.log("My number = " a "!")
}
```

## Constants
```
CONSTANT = "My Constant"

main(Env) {
	env.log("My constant: \"" CONSTANT "\"")
}
```

## Type Definitions
```
MyType(
	Sint32 my_integer
	String my_string
)

// Constructors (from) Functions
(Float32 my_float) -> MyType {
	MyType(
		my_integer = Sint32(my_float)
		my_string = String(my_string)
	)
}
	
(Char my_char) -> MyType {
	MyType(
		my_integer = Sint32(my_char)
		my_string = String(my_char)
	)
}

// Casting (to) Functions
(MyType) -> String {
	return my_type.my_string
}
	
(MyType) -> Sint32 {
	return my_type.my_integer
}

// Implementation Functions
MyType.debug() -> String {
	return "DEBUG: " self.my_string ", " self.my_integer "."
}

main(Env) {
	_ my_var = MyType(1.0)
	
	env.log(my_var.debug())
	env.log(String(my_var))
	env.log(Sint32(my_var))
}
```
