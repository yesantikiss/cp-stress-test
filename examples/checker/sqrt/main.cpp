#include <bits/stdc++.h>
using namespace std;

// x と -x のどちらも正解なので、a の偶奇でわざと符号を変えている
int main() {
    long long a;
    cin >> a;
    double x = sqrt((double)a);
    if (a % 2) x = -x;
    printf("%.10f\n", x);
}
