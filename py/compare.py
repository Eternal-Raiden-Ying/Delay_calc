import numpy as np
import argparse


parser = argparse.ArgumentParser(description='manual to this script')
parser.add_argument("--delay", type=bool, default=0)  
parser.add_argument("--path", type=str, default='delay0.txt')  # your results
parser.add_argument("--golden", type=str, default='Group0.txt')  # golden results

args = parser.parse_args()
delay_type = args.delay
self_path = args.path
golden_path = args.golden
goldens = {}
with open(golden_path, 'r') as file:  # golden results, please put in the same dir. as your own results (delay0.txt)
    for line in file:
        lines = line.strip("\n").split(" ")
        goldens[lines[0]+lines[1]] = lines[2]

# print(goldens.values())
test_smaller50_error = []
test_larger50_error = []

golden_list=[]
with open(self_path, 'r') as file:
    for line in file:
        lines = line.strip("\n").split(' ')
        s = float(lines[2])
        g = float(goldens[lines[0] + lines[1]]) * 1000
        golden_list.append(g)
        if g > 50:
            test_larger50_error.append(100 * abs(g - s) / g)
            if (100 * abs(g - s) / g)>1e10:
                print('here')
        else:
            test_smaller50_error.append(abs(g - s))

golden_np= np.asarray(golden_list)
test_smaller50_error = np.array(test_smaller50_error)
test_larger50_error = np.array(test_larger50_error)
print("**************TEST***************")

print("test_smaller50_mean:", np.mean(abs(test_smaller50_error)))
print("test_smaller50_max:", np.max(abs(test_smaller50_error)))
print("test_smaller50_2sigma:", 2*np.var(test_smaller50_error)**0.5)

print("test_larger50_mean:", np.mean(abs(test_larger50_error)))
print("test_larger50_max:", np.max(abs(test_larger50_error)))
print("test_larger50_2sigma:", 2*np.var(test_larger50_error)**0.5)
