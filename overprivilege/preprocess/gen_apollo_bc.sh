# Make sure /apollo/llvm_gen/apollo has been created
cd /apollo
s1=$(ls -l bazel-apollo | cut -d'.' -f1 | cut -d'>' -f2 | cut -d' ' -f2)
s2=$(ls -l bazel-apollo | cut -d'.' -f2)
dir=$(echo "$s1.$s2")
n=$(echo $dir | grep -o '/' | wc -l);
build_dir=$(echo $dir | cut -d'/' -f1-$(($n-1)))
cd /apollo/modules
find . -type f -name *.cc > /apollo/apollo_bitcode.meta
find . -type f -name *.c >> /apollo/apollo_bitcode.meta
find . -type f -name *.cpp >> /apollo/apollo_bitcode.meta
if [ ! -e /apollo/llvm_gen ]; then mkdir /apollo/llvm_gen; fi
if [ ! -e /apollo/llvm_gen/apollo ]; then mkdir /apollo/llvm_gen/apollo; fi
for f in $(cat /apollo/apollo_bitcode.meta | grep -v "\_test" | grep -v "\/tools\/" | grep  -v "\/tests\/" | grep  -v "\/test\/");
do
    a=$(python /apollo/bc_name.py $f)
    if [ -e /apollo/llvm_gen/apollo/$a ]; then continue; fi
    echo "Generating bc for $f"
#    clang -c -emit-llvm -std=c++11 -I /apollo/ -I /home/tmp/ -I /apollo/bazel-genfiles -I $build_dir/external/com_google_protobuf/src/ -I /apollo/bazel-genfiles/external/com_github_gflags_gflags/ -I $build_dir/external/eigen/ -I /usr/local/include/pcl-1.7/ -I /usr/include/vtk-5.8/ -I $build_dir/external/curlpp/include/ -I $build_dir/external/ -I $build_dir/external/local_integ/ -I $build_dir/external/civetweb/include/ -I /usr/local/ipopt/include/coin/ -I $build_dir/external/gtest/googlemock/include/ /apollo/modules/$f -o /apollo/llvm_gen/apollo/$a
    if [ $(echo $f | grep "\.cc\|\.cpp" | wc -l) -gt 0 ]; then 
#        clang -c -emit-llvm -std=c++11 -I /apollo/ -I /home/tmp/ -I /apollo/bazel-genfiles -I $build_dir/external/com_google_protobuf/src/ -I /apollo/bazel-genfiles/external/com_github_gflags_gflags/ -I $build_dir/external/eigen/ -I /usr/local/include/pcl-1.7/ -I /usr/include/vtk-5.8/ -I $build_dir/external/curlpp/include/ -I $build_dir/external/ -I $build_dir/external/local_integ/ -I $build_dir/external/civetweb/include/ -I /usr/local/ipopt/include/coin/ -I $build_dir/external/gtest/googlemock/include/ -I /apollo/modules/drivers/gnss/include/ -I /apollo/modules/drivers/usb_cam/include/ -I /apollo/modules/drivers/gnss/ /apollo/modules/$f -o /apollo/llvm_gen/apollo/$a
         clang -c -emit-llvm -std=c++11 -I /apollo/ -I /home/tmp/ -I /apollo/bazel-genfiles -I /apollo/third_party/can_card_library -I $build_dir/external/com_google_protobuf/src/ -I /apollo/bazel-genfiles/external/com_github_gflags_gflags/ -I $build_dir/external/eigen/ -I /usr/local/include/pcl-1.7/ -I /usr/include/vtk-5.8/ -I $build_dir/external/curlpp/include/ -I $build_dir/external/ -I $build_dir/external/local_integ/ -I $build_dir/external/civetweb/include/ -I /usr/local/ipopt/include/coin/ -I $build_dir/external/gtest/googlemock/include/ -I /apollo/modules/drivers/gnss/include -I /apollo/modules/drivers/usb_cam/include -I /apollo/modules/drivers/pandora/pandora_driver/src/Pandar40P/include -I /apollo/modules/drivers/pandora/pandora_driver/src/Pandar40P/ -I /apollo/modules/drivers/pandora/pandora_driver/include -I /apollo/modules/drivers/pandora/pandora_pointcloud/include -I /apollo/modules/drivers/velodyne/velodyne_pointcloud/include -I /apollo/modules/drivers/velodyne/velodyne_driver/include -I /apollo/modules/drivers/pandora/pandora_driver/ -I /apollo/module./drivers/pandora/pandora_driver/src/Pandar40P/ -I /apollo/bazel-genfiles/modules/drivers/gnss -I /apollo/modules/drivers/pandora/pandora_driver/ /apollo/modules/$f -o /apollo/llvm_gen/apollo/$a
    else
#        clang -c -emit-llvm -I /apollo/ -I /home/tmp/ -I /apollo/bazel-genfiles -I $build_dir/external/com_google_protobuf/src/ -I /apollo/bazel-genfiles/external/com_github_gflags_gflags/ -I $build_dir/external/eigen/ -I /usr/local/include/pcl-1.7/ -I /usr/include/vtk-5.8/ -I $build_dir/external/curlpp/include/ -I $build_dir/external/ -I $build_dir/external/local_integ/ -I $build_dir/external/civetweb/include/ -I /usr/local/ipopt/include/coin/ -I $build_dir/external/gtest/googlemock/include/ -I /apollo/modules/drivers/gnss/include/ -I /apollo/modules/drivers/usb_cam/include/ -I /apollo/modules/drivers/gnss/ -I /apollo/modules/drivers/pandora/pandora_driver/ /apollo/modules/$f -o /apollo/llvm_gen/apollo/$a
         clang -c -emit-llvm -I /apollo/ -I /home/tmp/ -I /apollo/bazel-genfiles -I $build_dir/external/com_google_protobuf/src/ -I /apollo/bazel-genfiles/external/com_github_gflags_gflags/ -I $build_dir/external/eigen/ -I /usr/local/include/pcl-1.7/ -I /usr/include/vtk-5.8/ -I $build_dir/external/curlpp/include/ -I $build_dir/external/ -I $build_dir/external/local_integ/ -I $build_dir/external/civetweb/include/ -I /usr/local/ipopt/include/coin/ -I $build_dir/external/gtest/googlemock/include/ -I /apollo/modules/drivers/gnss/include -I /apollo/modules/drivers/usb_cam/include -I /apollo/modules/drivers/pandora/pandora_driver/src/Pandar40P/include -I /apollo/modules/drivers/pandora/pandora_driver/src/Pandar40P/ -I /apollo/modules/drivers/pandora/pandora_driver/include -I /apollo/modules/drivers/pandora/pandora_pointcloud/include -I /apollo/modules/drivers/velodyne/velodyne_pointcloud/include -I /apollo/modules/drivers/velodyne/velodyne_driver/include -I /apollo/modules/drivers/pandora/pandora_driver/ -I /apollo/module./drivers/pandora/pandora_driver/src/Pandar40P/ -I /apollo/bazel-genfiles/modules/drivers/gnss -I /apollo/modules/drivers/pandora/pandora_driver/ /apollo/modules/$f -o /apollo/llvm_gen/apollo/$a
    fi
done
#find . -type f -name *.c > /apollo/driver_bitcode.meta
#find . -type f -name *.cpp >> /apollo/driver_bitcode.meta
for f in $(cat /apollo/driver_bitcode.meta | grep -v "\_test" | grep -v "\/tools\/" | grep  -v "\/tests\/" | grep  -v "\/test\/");
do
    a=$(python /apollo/bc_name.py $f)
    if [ -e /apollo/llvm_gen/apollo/$a ]; then continue; fi
    echo "Generating bc for $f"
    if  [ $(echo $f | grep "\.cc\|\.cpp" | wc -l) -gt 0 ]; then 
         clang -c -emit-llvm -std=c++11 -I /apollo/ -I /home/tmp/ -I /apollo/bazel-genfiles -I $build_dir/external/com_google_protobuf/src/ -I /apollo/bazel-genfiles/external/com_github_gflags_gflags/ -I $build_dir/external/eigen/ -I /usr/local/include/pcl-1.7/ -I /usr/include/vtk-5.8/ -I $build_dir/external/curlpp/include/ -I $build_dir/external/ -I $build_dir/external/local_integ/ -I $build_dir/external/civetweb/include/ -I /usr/local/ipopt/include/coin/ -I $build_dir/external/gtest/googlemock/include/ -I /apollo/modules/drivers/gnss/include -I /apollo/modules/drivers/usb_cam/include -I /apollo/modules/drivers/pandora/pandora_driver/src/Pandar40P/include -I /apollo/modules/drivers/pandora/pandora_driver/src/Pandar40P/ -I /apollo/modules/drivers/pandora/pandora_driver/include -I /apollo/modules/drivers/pandora/pandora_pointcloud/include -I /apollo/modules/drivers/velodyne/velodyne_pointcloud/include -I /apollo/modules/drivers/velodyne/velodyne_driver/include -I /apollo/modules/drivers/pandora/pandora_driver/ -I /apollo/module./drivers/pandora/pandora_driver/src/Pandar40P/ -I /apollo/bazel-genfiles/modules/drivers/gnss -I /apollo/modules/drivers/pandora/pandora_driver/ /apollo/modules/$f -o /apollo/llvm_gen/apollo/$a
    else
         clang -c -emit-llvm -I /apollo/ -I /home/tmp/ -I /apollo/bazel-genfiles -I $build_dir/external/com_google_protobuf/src/ -I /apollo/bazel-genfiles/external/com_github_gflags_gflags/ -I $build_dir/external/eigen/ -I /usr/local/include/pcl-1.7/ -I /usr/include/vtk-5.8/ -I $build_dir/external/curlpp/include/ -I $build_dir/external/ -I $build_dir/external/local_integ/ -I $build_dir/external/civetweb/include/ -I /usr/local/ipopt/include/coin/ -I $build_dir/external/gtest/googlemock/include/ -I /apollo/modules/drivers/gnss/include -I /apollo/modules/drivers/usb_cam/include -I /apollo/modules/drivers/pandora/pandora_driver/src/Pandar40P/include -I /apollo/modules/drivers/pandora/pandora_driver/src/Pandar40P/ -I /apollo/modules/drivers/pandora/pandora_driver/include -I /apollo/modules/drivers/pandora/pandora_pointcloud/include -I /apollo/modules/drivers/velodyne/velodyne_pointcloud/include -I /apollo/modules/drivers/velodyne/velodyne_driver/include -I /apollo/modules/drivers/pandora/pandora_driver/ -I /apollo/module./drivers/pandora/pandora_driver/src/Pandar40P/ -I /apollo/bazel-genfiles/modules/drivers/gnss -I /apollo/modules/drivers/pandora/pandora_driver/ /apollo/modules/$f -o /apollo/llvm_gen/apollo/$a
    fi
done