# -*- coding: utf-8 -*-
# @Time    : 2020/9/4 14:11
# @Author  : tmb
class TreeNode:
    def __init__(self, x):
        self.val = x
        self.left = None
        self.right = None


'''
pre[1,2,4,5,3,6,7]
in [4,2,5,1,6,3,7]
'''


class Solution:
    def buildTree(self, preorder, inorder):
        if not preorder:
            return None
        root = TreeNode(preorder[0])
        i = inorder.index(root.val)
        root.left = self.buildTree(preorder[1:i + 1], inorder[:i + 1])  # 索引到i+1，到i
        root.right = self.buildTree(preorder[i + 1:], inorder[i + 1:])
        return root


if __name__ == '__main__':
    a = Solution()
    b = a.buildTree([3, 9, 20, 15, 7], [9, 3, 15, 20, 7])
