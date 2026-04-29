<template>
  <aside class="sidebar">
    <div class="sidebar-header">
      <h1 class="app-title">MindVault</h1>
    </div>
    
    <div class="search-box">
      <input 
        type="text" 
        placeholder="搜索笔记... (Ctrl+K)" 
        class="search-input"
      />
    </div>

    <nav class="nav-section">
      <div class="nav-item active">
        <span class="nav-icon">📁</span>
        <span>文件</span>
      </div>
      <div class="nav-item">
        <span class="nav-icon">🏷️</span>
        <span>标签</span>
      </div>
      <div class="nav-item">
        <span class="nav-icon">⭐</span>
        <span>收藏</span>
      </div>
    </nav>

    <div class="file-tree">
      <div class="tree-header">
        <span>笔记</span>
        <button class="btn-new" @click="createNewNote">+ 新建</button>
      </div>
      <div class="tree-content">
        <div class="tree-item" v-for="note in notes" :key="note.id">
          <span class="file-icon">📝</span>
          <span class="file-name">{{ note.title }}</span>
        </div>
      </div>
    </div>
  </aside>
</template>

<script setup lang="ts">
import { ref } from 'vue';

interface Note {
  id: string;
  title: string;
}

const notes = ref<Note[]>([
  { id: '1', title: '欢迎使用 MindVault' },
  { id: '2', title: '快速入门指南' },
]);

const createNewNote = () => {
  console.log('创建新笔记');
};
</script>

<style scoped>
.sidebar {
  width: 280px;
  background: #1a1a1a;
  border-right: 1px solid #333;
  display: flex;
  flex-direction: column;
  padding: 16px;
}

.sidebar-header {
  margin-bottom: 16px;
}

.app-title {
  font-size: 20px;
  font-weight: 600;
  color: #fff;
  margin: 0;
}

.search-box {
  margin-bottom: 16px;
}

.search-input {
  width: 100%;
  padding: 8px 12px;
  background: #2a2a2a;
  border: 1px solid #333;
  border-radius: 6px;
  color: #fff;
  font-size: 14px;
}

.search-input::placeholder {
  color: #666;
}

.nav-section {
  margin-bottom: 16px;
}

.nav-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  border-radius: 6px;
  cursor: pointer;
  color: #999;
  transition: all 0.2s;
}

.nav-item:hover {
  background: #2a2a2a;
  color: #fff;
}

.nav-item.active {
  background: #2a2a2a;
  color: #fff;
}

.nav-icon {
  font-size: 16px;
}

.file-tree {
  flex: 1;
  overflow-y: auto;
}

.tree-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
  color: #999;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.btn-new {
  background: transparent;
  border: none;
  color: #0ea5e9;
  cursor: pointer;
  font-size: 12px;
}

.tree-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 8px;
  border-radius: 4px;
  cursor: pointer;
  color: #ccc;
  font-size: 14px;
}

.tree-item:hover {
  background: #2a2a2a;
}

.file-icon {
  font-size: 14px;
}

.file-name {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
</style>
