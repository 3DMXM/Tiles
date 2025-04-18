add_rules("mode.debug", "mode.release")

add_requires("inih", "libcurl", "opencv") 

 
target("mergeTiles") 
    set_kind("binary") 
    add_files("src/*.cpp") 
    set_languages("cxx20")
    add_packages("inih", "libcurl", "opencv")
    add_cxflags("/utf-8", {tools = "cl"})


