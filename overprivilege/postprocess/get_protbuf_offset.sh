# *.pb.h: four fields as internal fields
# TODO: locate fields and compute offsets automatically
file=$1
cat /apollo/bazel-genfiles/$1 | grep "class \|=\|private:" | grep "\/\/ optional\|\/\/ repeated\|class\|private:" 
