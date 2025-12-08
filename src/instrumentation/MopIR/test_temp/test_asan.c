int main(int argc, char *argv[]) {
    int arr[2];
    for (int i = 0; i < argc; i++) {
        arr[i] = i;
    }
    return arr[1];
}