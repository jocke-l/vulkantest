project('vulkantest', 'c', default_options: ['c_std=c23'])

glfw_dep = dependency('glfw3')
vulkan_dep = dependency('vulkan')

executable(
  'vulkantest',
  'main.c',
  dependencies: [glfw_dep, vulkan_dep],
  )
