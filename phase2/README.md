Authors: Amelia Matheson, Hudson Cox

Testcases we are requesting points for even though they do not pass the test cases:

- Test08: Child processes (of the same priorities) run in the order they should, but because they are dying in close succession of each other, 
    join reports each deaths right away before moving on to the next dying child

- Test13: Clock device status values do not perfectly match testcase output, but our values are near to 100k and 200k respectively

- Test30: All processes complete their tasks in the order they should, the deaths of dying child processes are reported right away by
    join before moving on to the next process

- Test31: All processes complete their tasks in the order they should, the deaths of dying child processes are reported right away by
    join before moving on to the next process

- Test46: Child processes (of the same priorities) run in the order they should, but because they are dying in close succession of each other, 
    join reports each deaths right away before moving on to the next dying child