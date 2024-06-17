# 先编译llvm-17
# git clone --depth 1 -b wf/17.x  git@code.byted.org:flowcompiler-toolchain/llvm-project.git
# bash build.sh 0
# export PATH=$HOME/.local/llvm-17-x86_64/bin:$PATH

current_dir=$(cd $(dirname $0); pwd)
export rebuild=$1 # 0-重新编译
TARGET_ARCH="x86_64"
#TARGET_ARCH="arm64"
CODON_MAJ_VER=0

# codon path
codon_src_dir=${current_dir}
codon_build_dir=${current_dir}/build
codon_install_dir=${HOME}/.local/codon-$CODON_MAJ_VER-$TARGET_ARCH
codon_dir=${codon_install_dir}/lib/cmake

# if [ ! -d "$codon_src_dir" ]; then
#     cd "$codon_src_dir"
#     git clone --depth 1 -b release/12.x git@code.byted.org:flowcompiler-toolchain/codon.git
# fi

function build_codon(){
    # 如果参数为0，则删除build目录，重新生成makefile，再编译
    if [ ! -z $rebuild ] && [ $rebuild == 0 ]; then
        echo ==== restrat to build codon, please input: y/n ====
        # read input
        input='y'
        echo =========input: $input =======
        if [ $input == 'y' ]; then
            echo ========= remove ${codon_build_dir} =======
            rm -rf ${codon_build_dir} ${codon_install_dir}
        fi
    fi

    if [ ! -d ${codon_build_dir} ]; then       
        mkdir -p ${codon_build_dir}
        mkdir -p ${codon_install_dir}

        CODON_TOOLS=" -DCMAKE_C_COMPILER="clang" \
                    -DCMAKE_CXX_COMPILER="clang++" \
                    -DLLVM_DIR=$(llvm-config --cmakedir) \
                  "

        cmake -G Ninja -S ${codon_src_dir} -B ${codon_build_dir} \
                -DCMAKE_BUILD_TYPE="Debug" \
                -DCMAKE_CXX_STANDARD=17 \
                -DCMAKE_INSTALL_PREFIX=${codon_install_dir} \
                ${CODON_TOOLS}
    fi
    ninja -C $build_codon -j $(nproc)
    # ninja -C ${codon_build_dir} -j $(nproc) install
}

build_codon

echo "Please add the follwing environment variables"
echo "export CODON_INS_DIR=$codon_install_dir"
echo "export CODON_DIR=$codon_dir"
echo "export PATH=\$CODON_INS_DIR/bin:\$PATH"
echo "./build/codon build -release -llvm test/fib.codon"
echo " export CODON_PATH=$PWD/stdlib"
