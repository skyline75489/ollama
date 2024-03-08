# common logic accross linux and darwin

init_vars() {
    case "${GOARCH}" in
    "amd64")
        ARCH="x86_64"
        ;;
    "arm64")
        ARCH="arm64"
        ;;
    *)
        ARCH=$(uname -m | sed -e "s/aarch64/arm64/g")
    esac

    LLAMACPP_DIR=../llama.cpp
    ORT_GENAI_DIR=../onnxruntime-genai

    CMAKE_TARGETS="--target ext_server"

    CMAKE_DEFS=""
    if echo "${CGO_CFLAGS}" | grep -- '-g' >/dev/null; then
        CMAKE_DEFS="-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_VERBOSE_MAKEFILE=on -DLLAMA_GPROF=on -DLLAMA_SERVER_VERBOSE=on ${CMAKE_DEFS}"
    else
        # TODO - add additional optimization flags...
        CMAKE_DEFS="-DCMAKE_BUILD_TYPE=Release -DLLAMA_SERVER_VERBOSE=off ${CMAKE_DEFS}"
    fi
    case $(uname -s) in 
    "Darwin")
        LIB_EXT="dylib"
        WHOLE_ARCHIVE="-Wl,-force_load"
        NO_WHOLE_ARCHIVE=""
        GCC_ARCH="-arch ${ARCH}"
        ;;
    "Linux")
        LIB_EXT="so"
        WHOLE_ARCHIVE="-Wl,--whole-archive"
        NO_WHOLE_ARCHIVE="-Wl,--no-whole-archive"

        # Cross compiling not supported on linux - Use docker
        GCC_ARCH=""
        ;;
    *)
        ;;
    esac
    if [ -z "${CMAKE_CUDA_ARCHITECTURES}" ] ; then 
        CMAKE_CUDA_ARCHITECTURES="50;52;61;70;75;80"
    fi
}

git_module_setup() {
    if [ -n "${OLLAMA_SKIP_PATCHING}" ]; then
        echo "Skipping submodule initialization"
        return
    fi
    # Make sure the tree is clean after the directory moves
    if [ -d "${LLAMACPP_DIR}/gguf" ]; then
        echo "Cleaning up old submodule"
        rm -rf ${LLAMACPP_DIR}
    fi
    git submodule init
    git submodule update --force ${LLAMACPP_DIR}

}

apply_patches() {
    apply_patches_llama_cpp
    apply_patches_onnxruntime_genai
}

apply_patches_llama_cpp() {
    # Wire up our CMakefile
    if ! grep ollama ${LLAMACPP_DIR}/examples/server/CMakeLists.txt; then
        echo 'include (../../../ext_server/CMakeLists.txt) # ollama' >>${LLAMACPP_DIR}/examples/server/CMakeLists.txt
    fi

    if [ -n "$(ls -A ../patches/*.diff)" ]; then
        # apply temporary patches until fix is upstream
        for patch in ../patches/*.diff; do
            for file in $(grep "^+++ " ${patch} | cut -f2 -d' ' | cut -f2- -d/); do
                (cd ${LLAMACPP_DIR}; git checkout ${file})
            done
        done
        for patch in ../patches/*.diff; do
            (cd ${LLAMACPP_DIR} && git apply ${patch})
        done
    fi

    # Avoid duplicate main symbols when we link into the cgo binary
    sed -e 's/int main(/int __main(/g' <${LLAMACPP_DIR}/examples/server/server.cpp >${LLAMACPP_DIR}/examples/server/server.cpp.tmp &&
        mv ${LLAMACPP_DIR}/examples/server/server.cpp.tmp ${LLAMACPP_DIR}/examples/server/server.cpp
}

apply_patches_onnxruntime_genai() {
    # Wire up our CMakefile
    if ! grep ollama ${ORT_GENAI_DIR}/CMakeLists.txt; then
        echo 'include (../ext_server/CMakeLists.ort.txt) # ollama' >>${ORT_GENAI_DIR}/CMakeLists.txt
    fi
}

build() {
    if [[ $BUILD_DIR =~ $LLAMACPP_DIR ]]; then
        cmake -S ${LLAMACPP_DIR} -B ${BUILD_DIR} ${CMAKE_DEFS}
        cmake --build ${BUILD_DIR} ${CMAKE_TARGETS} -j8
        mkdir -p ${BUILD_DIR}/lib/
        build_llama_cpp
    elif [[ $BUILD_DIR =~ $ORT_GENAI_DIR ]]; then
        cmake -S ${ORT_GENAI_DIR} -B ${BUILD_DIR} ${CMAKE_DEFS}
        cmake --build ${BUILD_DIR} ${CMAKE_TARGETS} -j8
        mkdir -p ${BUILD_DIR}/lib/
        build_onnxruntime_genai
    fi
}

build_llama_cpp() {
    g++ -fPIC -g -shared -o ${BUILD_DIR}/lib/libext_server.${LIB_EXT} \
        ${GCC_ARCH} \
        ${WHOLE_ARCHIVE} ${BUILD_DIR}/examples/server/libext_server.a ${NO_WHOLE_ARCHIVE} \
        ${BUILD_DIR}/common/libcommon.a \
        ${BUILD_DIR}/libllama.a \
        -Wl,-rpath,\$ORIGIN \
        -lpthread -ldl -lm \
        ${EXTRA_LIBS}
}

build_onnxruntime_genai() {
    cp ${BUILD_DIR}/libonnxruntime-genai.so ${BUILD_DIR}/lib/
    cp ${BUILD_DIR}/libonnxruntime.so.1.17.0 ${BUILD_DIR}/lib/

    g++ -fPIC -g -shared -o ${BUILD_DIR}/lib/libext_server.${LIB_EXT} \
        ${GCC_ARCH} \
        ${WHOLE_ARCHIVE} ${BUILD_DIR}/libext_server.a ${NO_WHOLE_ARCHIVE} \
        ${BUILD_DIR}/libonnxruntime-genai.so \
        ${BUILD_DIR}/libonnxruntime.so.1.17.0 \
        -Wl,-rpath,\$ORIGIN \
        -lpthread -ldl -lm \
        ${EXTRA_LIBS}
}

compress_libs() {
    echo "Compressing payloads to reduce overall binary size..."
    pids=""
    rm -rf ${BUILD_DIR}/lib/*.${LIB_EXT}*.gz
    for lib in ${BUILD_DIR}/lib/*.${LIB_EXT}* ; do
        echo "Compressing" $lib
        gzip --best -f ${lib} &
        pids+=" $!"
    done
    echo 
    for pid in ${pids}; do
        wait $pid
    done
    echo "Finished compression"
}

# Keep the local tree clean after we're done with the build
cleanup() {
    (cd ${LLAMACPP_DIR}/examples/server/ && git checkout CMakeLists.txt server.cpp)

    if [ -n "$(ls -A ../patches/*.diff)" ]; then
        for patch in ../patches/*.diff; do
            for file in $(grep "^+++ " ${patch} | cut -f2 -d' ' | cut -f2- -d/); do
                (cd ${LLAMACPP_DIR}; git checkout ${file})
            done
        done
    fi
}
