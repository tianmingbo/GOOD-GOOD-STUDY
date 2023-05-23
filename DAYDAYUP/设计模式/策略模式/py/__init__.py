"""
策略模式的主要角色有以下三种：
抽象策略（Strategy）：定义了所有具体策略类所共有的接口或抽象类，提供了执行策略算法的方法。
具体策略（ConcreteStrategy）：实现了抽象策略中定义的接口或抽象类，封装了一种具体的算法或行为，并向客户端提供一个可供调用的策略执行方法。
策略上下文（Context）：封装了策略对象的创建和调用过程，并提供一个方法来设置策略对象或调用策略对象的方法。
"""
