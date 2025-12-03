// 简单的C测试程序，用于生成LLVM IR

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    int result = 0;
    for (int i = 0; i < b; i++) {
        result = add(result, a);
    }
    return result;
}

int fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    int a = 5;
    int b = 3;
    
    int sum = add(a, b);
    int product = multiply(a, b);
    int fib = fibonacci(10);
    
    // 这些变量被使用，避免编译器优化
    if (sum > product) {
        return fib;
    } else {
        return sum + product;
    }
}