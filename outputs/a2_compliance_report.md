
## A2 POSIX Compliance Results

| Category                       | posixsh Pass Rate     | dash Pass Rate        | bash Pass Rate        |
|:------------------------------ | :-------------------: | :-------------------: | :-------------------: |
| Pipelines                       | 1/17 (5.9%)           | 15/17 (88.2%)         | 15/17 (88.2%)         |
| I/O Redirections                | 2/33 (6.1%)           | 9/33 (27.3%)          | 19/33 (57.6%)         |
| Quoting                         | 12/24 (50.0%)         | 19/24 (79.2%)         | 22/24 (91.7%)         |
| Word expansion (`$VAR`)         | 1/6 (16.7%)           | 4/6 (66.7%)           | 5/6 (83.3%)           |
| Word splitting                  | 1/49 (2.0%)           | 9/49 (18.4%)          | 10/49 (20.4%)         |
| Builtins (`cd`/`exit`/`pwd`/`echo`) | 2/8 (25.0%)           | 5/8 (62.5%)           | 4/8 (50.0%)           |
| Background & job control        | 2/19 (10.5%)          | 17/19 (89.5%)         | 18/19 (94.7%)         |
| Compound cmds (`if`/`while`/`for`) | 0.0% (Not Impl)       | 5/5 (100.0%)          | 5/5 (100.0%)          |
| Command substitution `$()`      | 0.0% (Not Impl)       | 22/30 (73.3%)         | 23/30 (76.7%)         |
| Here-documents (`<<`)           | 0.0% (Not Impl)       | 26/33 (78.8%)         | 25/33 (75.8%)         |
| Arithmetic expansion `$((..))`  | 0.0% (Not Impl)       | 28/37 (75.7%)         | 31/37 (83.8%)         |

## CSV
category,posixsh_passed,posixsh_total,dash_passed,dash_total,bash_passed,bash_total,posixsh_implemented
"Pipelines",1,17,15,17,15,17,1
"I/O Redirections",2,33,9,33,19,33,1
"Quoting",12,24,19,24,22,24,1
"Word expansion (`$VAR`)",1,6,4,6,5,6,1
"Word splitting",1,49,9,49,10,49,1
"Builtins (`cd`/`exit`/`pwd`/`echo`)",2,8,5,8,4,8,1
"Background & job control",2,19,17,19,18,19,1
"Compound cmds (`if`/`while`/`for`)",0,0,5,5,5,5,0
"Command substitution `$()`",0,0,22,30,23,30,0
"Here-documents (`<<`)",0,0,26,33,25,33,0
"Arithmetic expansion `$((..))`",0,0,28,37,31,37,0
