// This is the 'include file'
// clone.dsl will load it as an implicit class
// Each method in here will become a method on the implicit class

def myUtilityMethod(String msg) {
    println "myUtilityMethod running with: ${msg}"
}
