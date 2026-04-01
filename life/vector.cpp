#include <cstdio>
#include <vector>

int main()
{
    std::vector<char> source, dest;

    for (int i = 0; i < 100; ++i) {
        source.push_back('A');
        std::vector<char> dest = std::vector<char>(source);
        printf("size %d capacity %d\n", source.capacity(), dest.capacity());
    }
    return 0;
}
