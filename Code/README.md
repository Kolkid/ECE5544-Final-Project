# Final Project Code and Use Notes
-----------------------------------------------------------------------------------------------------
IMPORTANT: You MUST install `time` in order to run the tests and see the timing for each of the passes
Run these 2 cmds in the environment 
```bash
apt-get update
apt-get install -y time
```
This program has 3 different LICM modes: `licm-classic`, `licm-andersen`, `licm-steensgaard`

## Microbenchmark Tests
Steps to run test on desired mode:
1. Edit the makefile line and replace DESIRED_PASS with one of the three modes (default is `licm-andersen`)
```Makefile
LICM_PASS    = DESIRED_PASS
```
Example:
```Makefile
LICM_PASS    = licm-andersen
```
2. Go to the directory with `unifiedpass.cpp`
3. Run 
```bash
make
make tests
```
4. The output will show the results of the 5 microbenchmarks

Interpreting the results:
The results will be printed with the following information in the following order:
- List of every hoistable load
- List of all hoistable instructions
- Statistics before test program optimization
- Statistics after test program optimization
- Execution time of the pass on the test program

## NPB Macrobenchmark Tests
The NPB benchmark bitcode files are included in: `npb_bc/`
The two benchmark inputs used are: `ep.bc`,`cg.bc`

EP was used as a lightweight real-program smoke test. CG was used as the more memory-oriented benchmark because it contains more loop and memory-access behavior relevant to alias-aware load hoisting.
Before running the NPB tests, build the pass:
```bash
make
```
To run the LICM passes on the NPB bitcode:
```bash
mkdir -p results

for b in ep cg; do
  for p in classic andersen steensgaard; do
    echo "=== $b $p ==="
    { time opt -bugpoint-enable-legacy-pm=1 \
      -load-pass-plugin=build/unifiedpass.so \
      -passes="licm-$p" \
      npb_bc/${b}.bc \
      -o npb_bc/${b}-${p}.bc \
      > results/${b}-${p}.out; } \
      2> results/${b}-${p}.time
  done
done
```

To verify the optimized bitcode:
No output from the verifier means the optimized LLVM IR is structurally valid.
```bash
for b in ep cg; do
  for p in classic andersen steensgaard; do
    opt -passes='verify' npb_bc/${b}-${p}.bc -o /dev/null
  done
done
```

### Counting Hoisted Instructions
To count all reported hoisted instructions:
```bash
echo "benchmark,pass,hoisted_instruction_count" > results/npb_hoisted_instruction_counts.csv

for b in ep cg; do
  for p in classic andersen steensgaard; do
    count=$(grep -E "^[[:space:]]+%" results/${b}-${p}.out | wc -l)
    echo "$b,$p,$count" >> results/npb_hoisted_instruction_counts.csv
  done
done

cat results/npb_hoisted_instruction_counts.csv
```
To count only hoisted loads:
```bash
echo "benchmark,pass,hoisted_load_count" > results/npb_hoisted_load_counts.csv

for b in ep cg; do
  for p in classic andersen steensgaard; do
    count=$(grep -E "^[[:space:]]+%.*= load " results/${b}-${p}.out | wc -l)
    echo "$b,$p,$count" >> results/npb_hoisted_load_counts.csv
  done
done

cat results/npb_hoisted_load_counts.csv
```
-----------------------------------------------------------------------------------------------------
