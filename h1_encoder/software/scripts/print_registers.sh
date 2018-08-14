gcc -DTEST_DATA -I ../inc -o printregs printregs.c
chmod u+x printregs
./printregs | tee H-Series_1_SWReg_Map.csv
