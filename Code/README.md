# Final Project Code and Use Notes
-----------------------------------------------------------------------------------------------------
IMPORTANT: You MUST install time in order to run the tests and see the timing for each of the passes
Run these 2 cmds in the environment 
  apt-get update
  apt-get install -y time


This program has 3 different LICM modes: licm-classic, licm-andersen, licm-steensgaard

Steps to run test on desired mode:
1. Edit the makefile line and replace DESIRED TEST with one of the three modes (default is licm-andersen)
      LICM_PASS    = DESIRED TEST
2. Go to the directory with unifiedpass.cpp
3. Run: make 
4. Run: make tests
5. The output will show the results of the 5 micro benchmarks

Interpereting the results:
The results will be printed with the following information in the following order:
- List of every hoistable load
- List of all hoistable instrucitons
- Statistics before test program optimization
- Statistics after test program optimization
- Execution time of the pass on the test program

-----------------------------------------------------------------------------------------------------