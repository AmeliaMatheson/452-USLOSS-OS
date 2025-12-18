Authors: Amelia Matheson, Hudson Cox


Failing Testcases:

- Test 13: Due to a race, our dispatcher calls testcase_main before XXp3. Therefore, when TCM calls join, it is blocked. That is why it's status is listed as "Blocked" in the dumpProcesses table when XXp3 calls dumpProcesses

- Test 14: Like in test13, the dispatcher calls testcase_main before XXp3

- Test 16: Like in test13 and test14, the dispatcher calls testcase_main before XXp3

- Test 27: Race conditions produced different order of processes