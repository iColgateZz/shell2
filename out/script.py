#!/usr/bin/python3

# reading files
f1 = open("out/real_shell_output.txt", "r") 
f2 = open("out/psh_output.txt", "r") 

f1_data = list(f1.readlines())
f2_data = list(f2.readlines())

i = len(f1_data)
j = 0

while (j < i):
	# matching line1 from both files
	if f1_data[j] != f2_data[j]: 
		print("Line ", j + 1, ":")
		# else print that line from both files
		print("\tshe:", f1_data[j], end='')
		print("\tpsh:", f2_data[j], end='')
	j += 1

# closing files
f1.close()
f2.close()
