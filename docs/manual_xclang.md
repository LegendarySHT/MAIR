## Sanitizer Parameters Usage Guide
This is the user manual for `xclang`.

### Overview
Sanitizers are tools integrated into compilers to detect various types of bugs during the execution of a program. This document explains the usage of the following sanitizer parameters:
```
-tsan
-asan
-ubsan
-xsan
-fsanitize=<value>
-fno-sanitize=<value>
```
The `value` here can be `address`, `thread`, `undefined`.

#### 1. Enables our sanitizer
To use our sanitizer, you need at least one of the following parameters: 
```
-tsan
-asan
-ubsan
-xsan
```
These parameters will register our middle-end plugins.

`-tsan`: Use our thread sanitizer. This parameter will use the clang front end and our midend. It cannot be used with other conflicted sanitizer options, e.g., `-asan`.

`-asan`: Use our address sanitizer. This parameter will use the clang front end and our midend. It cannot be used with other conflicted sanitizer options, e.g., `-tsan`.

`-ubsan`: Use our undefined sanitizer. This parameter will use the clang front end.

`-xsan`: Use our xsan which integrates the above three sanitizers. This parameter will use the clang front end and our midend.

#### 2. Use the original sanitizer
If you want to use llvm's original sanitizer, you can use the following parameters:
```
-fsanitize=address
-fsanitize=thread
-fsanitize=undefined 
```
These parameters will be used directly with the original middle-end plugin.

Due to conflicts with the original sanitizer, you may not be able to turn on two different sanitizers at the same time.

#### ​3. Turn off sanitizers
To turn off the specified sanitizer, you can use the following parameters:
```
-fno-sanitize=address
-fno-sanitize=thread
-fno-sanitize=undefined 
```
These parameters can turn off the previously opened sanitizer. It works on both our sanitizers and the original sanitizers.
