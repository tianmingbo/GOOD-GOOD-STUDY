# -*- coding: utf-8 -*-
# @Time    : 2020/12/2 20:43
# @Author  : tmb
from typing import List


class Solution:
    def maxProfit(self, prices: List[int]) -> int:
        if len(prices) == 1:
            return 0
        dp = [[0] * 2 for _ in range(len(prices))]
        dp[0][0] = 0
        dp[0][1] = -prices[0]
        dp[1][0] = max(dp[0][0], dp[0][1] + prices[1])
        dp[1][1] = max(dp[0][1], - prices[1])
        for i in range(2, len(prices)):
            dp[i][0] = max(dp[i - 1][0], dp[i - 1][1] + prices[i])
            dp[i][1] = max(dp[i - 1][1], dp[i - 2][0] - prices[i])
        return dp[-1][0]


if __name__ == '__main__':
    a = Solution()
    print(a.maxProfit([1, 2, 3, 0, 2]))
