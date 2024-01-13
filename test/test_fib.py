def fibonacci(n):
    a = 0
    b = 1
    c = 0
    sum = 0
    count = n
    while count > 0:
        count = count - 1 
        a = b 
        b = sum
        sum = a + b    
    return sum

a = fibonacci(30)
print(a == 832040, "fibonacci(30) = ", a)