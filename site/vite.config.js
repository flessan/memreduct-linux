import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// BASE_PATH lets GitHub Pages deploys use /memreduct-linux/
export default defineConfig({
  plugins: [react()],
  base: process.env.BASE_PATH || '/',
  server: {
    host: '0.0.0.0',
    allowedHosts: true,
  },
  preview: {
    host: '0.0.0.0',
    allowedHosts: true,
  },
})
