add_rules("mode.debug", "mode.release")

add_requires("inih", "opencv") 
 
target("slice") 
    set_kind("binary") 
    add_files("src/*.cpp")
    set_languages("cxx20")
    add_packages("inih","opencv")
