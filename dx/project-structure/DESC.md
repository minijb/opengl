---
id: project-structure
title: 项目目录结构（各文件夹的作用）
brief: 本仓库(LearnOpenGL 学习项目 CG_Lab)每个文件夹的角色、是否参与构建/git 跟踪,以及新代码与新资产目录的落位/接线方法。
tags: [structure, directory, onboarding, cmake]
aliases: [目录结构, 文件夹作用, 项目布局]
father: null
---
# 项目目录结构（各文件夹的作用）

## 是什么
本仓库（opengl / CG_Lab，跟随 LearnOpenGL 课程的 C++20 + OpenGL 3.3 学习项目）的目录地图：每个文件夹是什么、放什么内容、是否参与构建、是否被 git 跟踪。

## 作用
新人（或隔几个月回来的自己）上手时快速定位"东西在哪、新东西放哪"；新增源文件、资产目录时知道往哪个 CMake 列表里接线。配套知识文件：

- `knowledge/folder-map.md` —— 全部目录一览表（角色/参与构建?/git 跟踪?）
- `knowledge/code-placement.md` —— 写代码时的落位规则（src/include/place/temp 怎么选）
- `knowledge/asset-dir-wiring.md` —— 新增资产目录（shader/texture/model 同类）的三步接线

## 与父级的关系
本 dx 是根单元（工作区首个 dx）。

## 与子级的关系（可选）
暂无子级。
