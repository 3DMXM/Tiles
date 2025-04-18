add_rules("mode.debug", "mode.release")

add_requires("inih", "libcurl") 

 
target("dlTiles") 
    set_kind("binary") 
    add_files("src/*.cpp") 
    set_languages("cxx20")
    add_packages("inih", "libcurl")
    add_configfiles("config.ini", {prefixdir = ""})


