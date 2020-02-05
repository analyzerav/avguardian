import sys

str = sys.argv[1].split('.')[1]
fname = ''
s = str.split('/')
for i in range(len(s)):
    if len(s[i]) > 0:
       fname = fname + '_' + s[i]
if len(fname) > 0:
   fname = fname + '.bc'
print fname
