import subprocess

# mangle header guards
subprocess.run(["find", "./include", "-type", "f", "-exec", "sed", "-i", "-r", "s/\\b(DIY[[:alnum:]|_]*(_HPP|_H))\\b/VTKM\\1/g", "{}", "+"])

fp = open("vtkm-mangle-list.txt")
tokens = [line.lstrip().rstrip() for line in fp if line.lstrip() and not line.lstrip().startswith("#")]

for t in tokens:
  sr = "s/\\b" + t + "\\b/VTKM" + t + "/g"
  subprocess.run(["find", "./include", "-type", "f", "-exec", "sed", "-i", "-r", sr, "{}", "+"])

subprocess.run(["sed", "-i", "-r", "s/-D(DIY[[:alnum:]|_]*)/-DVTKM\\1/g", "CMakeLists.txt"])
