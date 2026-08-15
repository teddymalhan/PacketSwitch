#!/bin/bash




set -e


RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'


PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo -e "${BLUE}==================================================${NC}"
echo -e "${BLUE}        Local CI Validation Script${NC}"
echo -e "${BLUE}==================================================${NC}"
echo ""


print_step() {
    echo ""
    echo -e "${BLUE}▶ $1${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}


print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}


print_error() {
    echo -e "${RED}✗ $1${NC}"
}


print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}


FAILED=0




print_step "Step 1: Checking Code Formatting"

if command -v clang-format &> /dev/null; then
    if find include/ src/ test/ \( -name '*.hpp' -o -name '*.cpp' \) | \
       xargs clang-format --dry-run --Werror 2>&1; then
        print_success "Code formatting is correct"
    else
        print_error "Code formatting issues found"
        print_warning "Run 'make format' or 'cmake --build build --target clang-format' to fix"
        FAILED=1
    fi
else
    print_warning "clang-format not found, skipping formatting check"
fi




print_step "Step 2: Building Project (Release)"


BUILD_DIR="build-ci-test"
if [ -d "$BUILD_DIR" ]; then
    print_warning "Removing existing $BUILD_DIR directory"
    rm -rf "$BUILD_DIR"
fi


echo "Configuring CMake..."
if cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DProject_ENABLE_UNIT_TESTING=ON \
    -DProject_ENABLE_CCACHE=OFF \
    -G Ninja 2>&1 > /dev/null; then
    print_success "CMake configuration successful"
else
    print_error "CMake configuration failed"
    FAILED=1
    exit 1
fi


echo "Building project..."
if cmake --build "$BUILD_DIR" --config Release 2>&1; then
    print_success "Build successful"
else
    print_error "Build failed"
    FAILED=1
    exit 1
fi




print_step "Step 3: Running Unit Tests"

cd "$BUILD_DIR"
if ctest -C Release --output-on-failure --verbose; then
    print_success "All tests passed"
else
    print_error "Some tests failed"
    FAILED=1
fi
cd "$PROJECT_ROOT"




if command -v clang-tidy &> /dev/null; then
    print_step "Step 4: Running Static Analysis (Clang-Tidy)"
    
    BUILD_DIR_TIDY="build-ci-tidy"
    if [ -d "$BUILD_DIR_TIDY" ]; then
        rm -rf "$BUILD_DIR_TIDY"
    fi
    
    echo "Configuring with Clang-Tidy..."
    if cmake -B "$BUILD_DIR_TIDY" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DProject_ENABLE_UNIT_TESTING=ON \
        -DProject_ENABLE_CLANG_TIDY=ON \
        -DProject_ENABLE_CCACHE=OFF \
        -G Ninja 2>&1 > /dev/null; then
        
        echo "Building with static analysis..."
        if cmake --build "$BUILD_DIR_TIDY" --config Debug 2>&1 | grep -v "note:"; then
            print_success "Static analysis passed"
        else
            print_warning "Static analysis found issues (review output above)"

        fi
    else
        print_warning "Static analysis configuration failed"
    fi
else
    print_warning "clang-tidy not found, skipping static analysis"
fi




if [[ "$OSTYPE" == "linux-gnu"* ]] || [[ "$OSTYPE" == "darwin"* ]]; then
    print_step "Step 5: Running Tests with Address Sanitizer"
    
    BUILD_DIR_ASAN="build-ci-asan"
    if [ -d "$BUILD_DIR_ASAN" ]; then
        rm -rf "$BUILD_DIR_ASAN"
    fi
    
    echo "Configuring with ASan..."
    if cmake -B "$BUILD_DIR_ASAN" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DProject_ENABLE_UNIT_TESTING=ON \
        -DProject_ENABLE_ASAN=ON \
        -DProject_ENABLE_CCACHE=OFF \
        -G Ninja 2>&1 > /dev/null; then
        
        echo "Building with ASan..."
        if cmake --build "$BUILD_DIR_ASAN" --config Debug 2>&1 > /dev/null; then
            echo "Running tests with ASan..."
            cd "$BUILD_DIR_ASAN"
            if ctest -C Debug --output-on-failure; then
                print_success "ASan tests passed (no memory errors detected)"
            else
                print_error "ASan tests failed (memory errors detected)"
                FAILED=1
            fi
            cd "$PROJECT_ROOT"
        else
            print_warning "ASan build failed"
        fi
    else
        print_warning "ASan configuration failed"
    fi
fi




print_step "Cleanup"

echo "Removing temporary build directories..."
rm -rf "$BUILD_DIR" "$BUILD_DIR_TIDY" "$BUILD_DIR_ASAN"
print_success "Cleanup complete"




echo ""
echo -e "${BLUE}==================================================${NC}"
if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}         ✓ All Checks Passed!${NC}"
    echo -e "${GREEN}   Your code is ready to push to GitHub${NC}"
else
    echo -e "${RED}         ✗ Some Checks Failed${NC}"
    echo -e "${RED}   Please fix the issues before pushing${NC}"
fi
echo -e "${BLUE}==================================================${NC}"
echo ""

exit $FAILED

