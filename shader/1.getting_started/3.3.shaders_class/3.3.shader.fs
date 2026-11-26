#version 330 core
out vec4 FragColor;

in vec3 ourColor;
uniform vec3 uniColor;

void main()
{
    vec3 newColor = ourColor + uniColor;
    newColor = clamp(newColor, vec3(0.0), vec3(1.0));
    FragColor = vec4(newColor, 1.0f);
}