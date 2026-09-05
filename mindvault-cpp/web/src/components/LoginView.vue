<script setup lang="ts">
import { ref } from 'vue'

const emit = defineEmits<{
  login: [user: { id: number; username: string; nickname: string; token: string }]
}>()

const mode = ref<'login' | 'register'>('login')
const username = ref('')
const password = ref('')
const nickname = ref('')
const loading = ref(false)
const error = ref('')

async function handleSubmit() {
  if (!username.value || !password.value) {
    error.value = '请填写用户名和密码'
    return
  }

  loading.value = true
  error.value = ''

  try {
    const endpoint = mode.value === 'login' ? '/api/auth/login' : '/api/auth/register'
    const body: any = {
      username: username.value,
      password: password.value,
    }
    if (mode.value === 'register' && nickname.value) {
      body.nickname = nickname.value
    }

    const res = await fetch(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })

    const data = await res.json()

    if (data.ok) {
      // 保存 token
      localStorage.setItem('token', data.data.token)
      localStorage.setItem('user', JSON.stringify(data.data))
      emit('login', data.data)
    } else {
      error.value = data.error?.message || '操作失败'
    }
  } catch (e) {
    error.value = '网络错误，请重试'
  } finally {
    loading.value = false
  }
}

function toggleMode() {
  mode.value = mode.value === 'login' ? 'register' : 'login'
  error.value = ''
}
</script>

<template>
  <div class="login-page">
    <div class="login-card">
      <div class="login-header">
        <h1 class="login-logo">🧠 MindVault</h1>
        <p class="login-subtitle">本地知识管理系统</p>
      </div>

      <div class="login-tabs">
        <button :class="{ active: mode === 'login' }" @click="mode = 'login'">登录</button>
        <button :class="{ active: mode === 'register' }" @click="mode = 'register'">注册</button>
      </div>

      <form @submit.prevent="handleSubmit" class="login-form">
        <div class="form-group">
          <label>用户名</label>
          <input v-model="username" type="text" placeholder="请输入用户名" autocomplete="username" />
        </div>

        <div class="form-group">
          <label>密码</label>
          <input v-model="password" type="password" placeholder="请输入密码" autocomplete="current-password" />
        </div>

        <div v-if="mode === 'register'" class="form-group">
          <label>昵称</label>
          <input v-model="nickname" type="text" placeholder="请输入昵称（可选）" />
        </div>

        <div v-if="error" class="error-message">{{ error }}</div>

        <button type="submit" class="btn-submit" :disabled="loading">
          {{ loading ? '处理中...' : (mode === 'login' ? '登录' : '注册') }}
        </button>
      </form>

      <div class="login-footer">
        <p v-if="mode === 'login'">
          还没有账号？<a href="#" @click.prevent="toggleMode">立即注册</a>
        </p>
        <p v-else>
          已有账号？<a href="#" @click.prevent="toggleMode">立即登录</a>
        </p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.login-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-primary);
  padding: 20px;
}

.login-card {
  width: 100%;
  max-width: 400px;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 40px 32px;
  box-shadow: var(--shadow-xl);
}

.login-header {
  text-align: center;
  margin-bottom: 32px;
}

.login-logo {
  font-size: 28px;
  font-weight: 700;
  color: var(--accent);
  margin: 0 0 8px 0;
}

.login-subtitle {
  font-size: 14px;
  color: var(--text-muted);
  margin: 0;
}

.login-tabs {
  display: flex;
  gap: 4px;
  background: var(--bg-primary);
  border-radius: var(--radius-sm);
  padding: 4px;
  margin-bottom: 24px;
}

.login-tabs button {
  flex: 1;
  padding: 8px 16px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-muted);
  font-size: 14px;
  cursor: pointer;
  transition: all var(--duration-fast);
}

.login-tabs button.active {
  background: var(--bg-secondary);
  color: var(--text-primary);
}

.login-form {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.form-group {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.form-group label {
  font-size: 13px;
  color: var(--text-muted);
}

.form-group input {
  padding: 10px 14px;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-primary);
  color: var(--text-primary);
  font-size: 14px;
  outline: none;
  transition: border-color var(--duration-fast);
}

.form-group input:focus {
  border-color: var(--accent);
}

.error-message {
  padding: 8px 12px;
  background: var(--red-glow);
  border: 1px solid rgba(239, 68, 68, 0.2);
  border-radius: var(--radius-sm);
  color: var(--red);
  font-size: 13px;
}

.btn-submit {
  padding: 12px;
  border: none;
  border-radius: var(--radius-sm);
  background: var(--accent);
  color: #fff;
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--duration-fast);
}

.btn-submit:hover:not(:disabled) {
  background: var(--accent-hover);
}

.btn-submit:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.login-footer {
  text-align: center;
  margin-top: 24px;
}

.login-footer p {
  font-size: 13px;
  color: var(--text-muted);
  margin: 0;
}

.login-footer a {
  color: var(--accent);
  text-decoration: none;
}

.login-footer a:hover {
  text-decoration: underline;
}
</style>
